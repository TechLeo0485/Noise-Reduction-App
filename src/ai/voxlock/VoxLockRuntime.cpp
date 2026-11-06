#include "ai/voxlock/VoxLockRuntime.h"

#include "ai/voxlock/OnnxModelPatch.h"
#include "core/RealtimeThread.h"

#include <windows.h>

// The runtime is loaded with LoadLibrary, so the C++ API is initialised by hand (Ort::InitApi) and nothing
// links against onnxruntime.lib.
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#if defined(VOICESTUDIO_HAS_DIRECTML)
#include <dml_provider_factory.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <mutex>

namespace vs::voxlock {

namespace {

constexpr const char* kEnrollFile = "enroll.onnx";
constexpr const char* kChunkFile = "chunk_int8_full.onnx";
constexpr const char* kSpeakerFile = "3dspeaker_speech_campplus_sv_en_voxceleb_16k.onnx";
// Relative to the models folder (the VoxLock files are in models/voxlock).
constexpr const char* kVoiceFilterFile = "voicefilter/voicefilter_lite.onnx";

// One ONNX Runtime per process. Deliberately never unloaded: sessions may still be finishing on other
// threads during shutdown, and unloading the DLL then would crash.
struct OrtProcess {
    std::mutex mutex;
    bool attempted = false;
    std::string error;
    std::string version;
    std::unique_ptr<Ort::Env> env;
    bool directMl = false; // DirectML.dll loaded and the runtime has the DirectML engine
};

OrtProcess& ortProcess() {
    static auto* process = new OrtProcess();
    return *process;
}

Ort::Env* acquireEnv(const std::filesystem::path& dllDirectory, std::string& error) {
    OrtProcess& p = ortProcess();
    std::lock_guard lock(p.mutex);
    if (!p.attempted) {
        p.attempted = true;
#if defined(VOICESTUDIO_HAS_DIRECTML)
        // Our DirectML.dll first, by full path: Windows has an older one in System32 that ONNX Runtime would
        // otherwise pick up. Once loaded, ONNX Runtime's own LoadLibrary("DirectML.dll") gets this module.
        const std::filesystem::path dmlDll = dllDirectory / L"DirectML.dll";
        p.directMl = LoadLibraryExW(dmlDll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) != nullptr;
#endif
        const std::filesystem::path dll = dllDirectory / L"onnxruntime.dll";
        HMODULE module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (module == nullptr) {
            p.error = "Cannot load " + dll.string() + " (error " + std::to_string(GetLastError()) + ")";
        } else {
            using GetApiBaseFn = const OrtApiBase*(ORT_API_CALL*)();
            auto getApiBase = reinterpret_cast<GetApiBaseFn>(GetProcAddress(module, "OrtGetApiBase"));
            const OrtApiBase* base = getApiBase != nullptr ? getApiBase() : nullptr;
            const OrtApi* api = base != nullptr ? base->GetApi(ORT_API_VERSION) : nullptr;
            p.version = base != nullptr ? base->GetVersionString() : "";
            if (api == nullptr) {
                p.error = "onnxruntime.dll does not provide ONNX Runtime API version " + std::to_string(ORT_API_VERSION) +
                          " (an older copy?)";
            } else {
                Ort::InitApi(api);
                try {
                    p.env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "VoxLock");
                } catch (const std::exception& e) {
                    p.error = std::string("ONNX Runtime initialisation failed: ") + e.what();
                }
            }
        }
    }
    error = p.error;
    return p.env.get();
}

bool readFile(const std::filesystem::path& path, std::vector<char>& out, std::string& error) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        error = "Model file missing: " + path.string();
        return false;
    }
    out.resize(static_cast<std::size_t>(in.tellg()));
    in.seekg(0);
    in.read(out.data(), static_cast<std::streamsize>(out.size()));
    if (!in) {
        error = "Cannot read " + path.string();
        return false;
    }
    return true;
}

// ONNX Runtime's intra-op pool threads: created here so they get the same "Pro Audio" MMCSS scheduling as
// the isolation worker that drives them (otherwise a busy desktop could stall a half-finished inference).
struct ThreadStart {
    OrtThreadWorkerFn fn;
    void* param;
};

DWORD WINAPI ortThreadMain(LPVOID raw) {
    auto* start = static_cast<ThreadStart*>(raw);
    setCurrentThreadName(L"VoxLock inference");
    preferPerformanceCores();
    {
        ScopedMmcss mmcss(L"Pro Audio");
        start->fn(start->param);
    }
    delete start;
    return 0;
}

OrtCustomThreadHandle createOrtThread(void*, OrtThreadWorkerFn fn, void* param) {
    auto* start = new ThreadStart{fn, param};
    HANDLE thread = CreateThread(nullptr, 0, ortThreadMain, start, 0, nullptr);
    if (thread == nullptr) {
        delete start;
        return nullptr;
    }
    return reinterpret_cast<OrtCustomThreadHandle>(thread);
}

void joinOrtThread(OrtCustomThreadHandle handle) {
    auto thread = reinterpret_cast<HANDLE>(const_cast<OrtCustomHandleType*>(handle));
    if (thread != nullptr) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
}

Ort::SessionOptions baseOptions(int threads, bool allowSpinning) {
    Ort::SessionOptions so;
    so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    so.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    so.SetIntraOpNumThreads(std::max(1, threads));
    so.SetInterOpNumThreads(1);
    so.AddConfigEntry("session.intra_op.allow_spinning", allowSpinning ? "1" : "0");
    so.SetCustomCreateThreadFn(createOrtThread);
    so.SetCustomJoinThreadFn(joinOrtThread);
    return so;
}

// Adds the DirectML engine for GPU `device` (DXGI adapter index) to `so`.
bool appendDirectMl(Ort::SessionOptions& so, int device, std::string& error) {
#if defined(VOICESTUDIO_HAS_DIRECTML)
    if (!ortProcess().directMl) {
        error = "DirectML.dll could not be loaded";
        return false;
    }
    // DirectML requirements: no memory pattern optimisation, sequential execution (set in baseOptions).
    so.DisableMemPattern();
    const OrtDmlApi* dml = nullptr;
    Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void**>(&dml)));
    if (dml == nullptr) {
        error = "this ONNX Runtime has no DirectML engine";
        return false;
    }
    Ort::ThrowOnError(dml->SessionOptionsAppendExecutionProvider_DML(so, device));
    return true;
#else
    (void)so;
    (void)device;
    error = "this build has no GPU engine (DirectML)";
    return false;
#endif
}

class OrtChunkModel final : public IChunkModel {
public:
    OrtChunkModel(Ort::Session session, int framesPerRun)
        : session_(std::move(session)), framesPerRun_(framesPerRun),
          memory_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
        wavShape_ = {1, chunkInputSamples(framesPerRun)};
        framesShape_ = {1, framesPerRun, kNfft};
    }

    [[nodiscard]] int framesPerRun() const noexcept override { return framesPerRun_; }

    bool run(const float* wav, const float* hIn, const float* cIn, float* frames, float* hOut, float* cOut) noexcept override {
        try {
            constexpr std::array<std::int64_t, 3> stateShape{kLstmLayers, kBands, kHidden};
            std::array<Ort::Value, 3> inputs{
                Ort::Value::CreateTensor<float>(memory_, const_cast<float*>(wav), static_cast<std::size_t>(wavShape_[1]),
                                                wavShape_.data(), wavShape_.size()),
                Ort::Value::CreateTensor<float>(memory_, const_cast<float*>(hIn), kStateSize, stateShape.data(), stateShape.size()),
                Ort::Value::CreateTensor<float>(memory_, const_cast<float*>(cIn), kStateSize, stateShape.data(), stateShape.size())};
            std::array<Ort::Value, 3> outputs{
                Ort::Value::CreateTensor<float>(memory_, frames, static_cast<std::size_t>(framesPerRun_) * kNfft,
                                                framesShape_.data(), framesShape_.size()),
                Ort::Value::CreateTensor<float>(memory_, hOut, kStateSize, stateShape.data(), stateShape.size()),
                Ort::Value::CreateTensor<float>(memory_, cOut, kStateSize, stateShape.data(), stateShape.size())};
            static constexpr std::array<const char*, 3> inputNames{"wav", "h", "c"};
            static constexpr std::array<const char*, 3> outputNames{"frames", "h_out", "c_out"};
            session_.Run(runOptions_, inputNames.data(), inputs.data(), inputs.size(), outputNames.data(), outputs.data(),
                         outputs.size());
            return true;
        } catch (...) {
            return false;
        }
    }

private:
    Ort::Session session_;
    int framesPerRun_;
    Ort::MemoryInfo memory_;
    Ort::RunOptions runOptions_;
    std::array<std::int64_t, 2> wavShape_{};
    std::array<std::int64_t, 3> framesShape_{};
};

// The "Lightweight filter" (VoiceFilter-Lite design, tools/voicefilter): same streaming contract as chunk.onnx, but
// a smaller LSTM state of shape (L, 1, H) kept in the first L*H values of the stream's state buffers, and output
// frames that are plain synthesis-windowed audio (Stream expects them divided by kOutputGain).
class OrtVoiceFilterModel final : public IChunkModel {
public:
    OrtVoiceFilterModel(Ort::Session session, int framesPerRun, std::array<std::int64_t, 3> stateShape)
        : session_(std::move(session)), framesPerRun_(framesPerRun), stateShape_(stateShape),
          stateCount_(static_cast<std::size_t>(stateShape[0] * stateShape[1] * stateShape[2])),
          memory_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
        wavShape_ = {1, chunkInputSamples(framesPerRun)};
        framesShape_ = {1, framesPerRun, kNfft};
    }

    [[nodiscard]] int framesPerRun() const noexcept override { return framesPerRun_; }

    bool run(const float* wav, const float* hIn, const float* cIn, float* frames, float* hOut, float* cOut) noexcept override {
        try {
            std::array<Ort::Value, 3> inputs{
                Ort::Value::CreateTensor<float>(memory_, const_cast<float*>(wav), static_cast<std::size_t>(wavShape_[1]),
                                                wavShape_.data(), wavShape_.size()),
                Ort::Value::CreateTensor<float>(memory_, const_cast<float*>(hIn), stateCount_, stateShape_.data(), stateShape_.size()),
                Ort::Value::CreateTensor<float>(memory_, const_cast<float*>(cIn), stateCount_, stateShape_.data(), stateShape_.size())};
            const std::size_t frameCount = static_cast<std::size_t>(framesPerRun_) * kNfft;
            std::array<Ort::Value, 3> outputs{
                Ort::Value::CreateTensor<float>(memory_, frames, frameCount, framesShape_.data(), framesShape_.size()),
                Ort::Value::CreateTensor<float>(memory_, hOut, stateCount_, stateShape_.data(), stateShape_.size()),
                Ort::Value::CreateTensor<float>(memory_, cOut, stateCount_, stateShape_.data(), stateShape_.size())};
            static constexpr std::array<const char*, 3> inputNames{"wav", "h", "c"};
            static constexpr std::array<const char*, 3> outputNames{"frames", "h_out", "c_out"};
            session_.Run(runOptions_, inputNames.data(), inputs.data(), inputs.size(), outputNames.data(), outputs.data(),
                         outputs.size());
            constexpr float kToStream = 1.0f / kOutputGain;
            for (std::size_t i = 0; i < frameCount; ++i) {
                frames[i] *= kToStream;
            }
            return true;
        } catch (...) {
            return false;
        }
    }

private:
    Ort::Session session_;
    int framesPerRun_;
    std::array<std::int64_t, 3> stateShape_;
    std::size_t stateCount_;
    Ort::MemoryInfo memory_;
    Ort::RunOptions runOptions_;
    std::array<std::int64_t, 2> wavShape_{};
    std::array<std::int64_t, 3> framesShape_{};
};

// CAM++ speaker embedding: fbank (T, 80) -> 512 values, returned at unit length.
class OrtSpeakerModel final : public ISpeakerModel {
public:
    explicit OrtSpeakerModel(Ort::Session session)
        : session_(std::move(session)), memory_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
        Ort::AllocatorWithDefaultOptions allocator;
        inputName_ = session_.GetInputNameAllocated(0, allocator).get();
        outputName_ = session_.GetOutputNameAllocated(0, allocator).get();
    }

    bool embed(const float* audio16k, std::size_t count, std::vector<float>& out) override {
        const int frames = computeFbank(audio16k, count, features_);
        if (frames < 10) {
            return false;
        }
        try {
            const std::array<std::int64_t, 3> shape{1, frames, kFbankBins};
            Ort::Value input = Ort::Value::CreateTensor<float>(memory_, features_.data(), features_.size(), shape.data(),
                                                               shape.size());
            const char* inputName = inputName_.c_str();
            const char* outputName = outputName_.c_str();
            std::vector<Ort::Value> outputs = session_.Run(Ort::RunOptions{nullptr}, &inputName, &input, 1, &outputName, 1);
            const auto info = outputs[0].GetTensorTypeAndShapeInfo();
            const float* p = outputs[0].GetTensorData<float>();
            out.assign(p, p + info.GetElementCount());
        } catch (...) {
            return false;
        }
        double norm = 0.0;
        for (float v : out) {
            if (!std::isfinite(v)) {
                return false;
            }
            norm += static_cast<double>(v) * v;
        }
        if (norm <= 0.0) {
            return false;
        }
        const auto scale = static_cast<float>(1.0 / std::sqrt(norm));
        for (float& v : out) {
            v *= scale;
        }
        return true;
    }

private:
    Ort::Session session_;
    Ort::MemoryInfo memory_;
    std::string inputName_;
    std::string outputName_;
    std::vector<float> features_;
};

} // namespace

struct Runtime::Impl {
    Ort::Env* env = nullptr;
    std::filesystem::path modelDirectory;
    std::unique_ptr<Ort::Session> enrollSession;
    std::vector<char> chunkModelBytes; // kept to bind each new enrollment into a fresh session
    std::vector<char> voiceFilterBytes; // read on first use
};

Runtime::Runtime(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Runtime::~Runtime() = default;

std::unique_ptr<Runtime> Runtime::create(const std::filesystem::path& dllDirectory,
                                         const std::filesystem::path& modelDirectory, std::string& error) {
    Ort::Env* env = acquireEnv(dllDirectory, error);
    if (env == nullptr) {
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->env = env;
    impl->modelDirectory = modelDirectory;
    std::vector<char> enrollBytes;
    if (!readFile(modelDirectory / kEnrollFile, enrollBytes, error) ||
        !readFile(modelDirectory / kChunkFile, impl->chunkModelBytes, error)) {
        return nullptr;
    }
    try {
        Ort::SessionOptions so = baseOptions(2, false);
        impl->enrollSession = std::make_unique<Ort::Session>(*env, enrollBytes.data(), enrollBytes.size(), so);
    } catch (const std::exception& e) {
        error = std::string("Cannot load enroll.onnx: ") + e.what();
        return nullptr;
    }
    return std::unique_ptr<Runtime>(new Runtime(std::move(impl)));
}

bool Runtime::enroll(const std::vector<float>& sample, EnrollmentTensors& out, std::string& error) {
    if (sample.size() != static_cast<std::size_t>(kEnrollSamples)) {
        error = "voice sample must be exactly 3 s";
        return false;
    }
    try {
        Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<std::int64_t, 2> shape{1, kEnrollSamples};
        Ort::Value input = Ort::Value::CreateTensor<float>(memory, const_cast<float*>(sample.data()), sample.size(),
                                                           shape.data(), shape.size());
        const char* inputName = "enroll";
        const std::array<const char*, 2> outputNames{"enroll_mag", "context"};
        std::vector<Ort::Value> outputs =
            impl_->enrollSession->Run(Ort::RunOptions{nullptr}, &inputName, &input, 1, outputNames.data(), outputNames.size());
        auto copyOut = [](Ort::Value& v, std::vector<float>& data, std::vector<std::int64_t>& dims) {
            auto info = v.GetTensorTypeAndShapeInfo();
            dims = info.GetShape();
            const float* p = v.GetTensorData<float>();
            data.assign(p, p + info.GetElementCount());
        };
        copyOut(outputs[0], out.mag, out.magDims);
        copyOut(outputs[1], out.context, out.contextDims);
    } catch (const std::exception& e) {
        error = std::string("Voice enrollment failed: ") + e.what();
        return false;
    }
    for (float v : out.mag) {
        if (!std::isfinite(v)) {
            error = "Voice enrollment produced invalid values";
            return false;
        }
    }
    for (float v : out.context) {
        if (!std::isfinite(v)) {
            error = "Voice enrollment produced invalid values";
            return false;
        }
    }
    return true;
}

std::unique_ptr<IChunkModel> Runtime::createChunkModel(const EnrollmentTensors& enrollment, const ChunkModelOptions& options,
                                                       std::string& error) {
    const int k = std::clamp(options.framesPerRun, 1, 16);
    std::vector<char> bound;
    const std::vector<ConstantTensor> constants{
        {"enroll_mag", enrollment.magDims, enrollment.mag.data()},
        {"context", enrollment.contextDims, enrollment.context.data()}};
    if (!bindInputsAsConstants(impl_->chunkModelBytes, constants, bound, error)) {
        error = "Cannot prepare chunk_int8_full.onnx: " + error;
        return nullptr;
    }
    try {
        Ort::SessionOptions so = baseOptions(options.threads, options.allowSpinning);
        // Static shapes let the optimiser fold the remaining shape arithmetic as well.
        so.AddFreeDimensionOverrideByName("S", chunkInputSamples(k));
        so.AddFreeDimensionOverrideByName("K", k);
        if (options.dmlDevice >= 0 && !appendDirectMl(so, options.dmlDevice, error)) {
            return nullptr;
        }
        Ort::Session session(*impl_->env, bound.data(), bound.size(), so);
        return std::make_unique<OrtChunkModel>(std::move(session), k);
    } catch (const std::exception& e) {
        error = std::string("Cannot load chunk_int8_full.onnx: ") + e.what();
        return nullptr;
    }
}

std::unique_ptr<IChunkModel> Runtime::createVoiceFilterModel(const std::vector<float>& dvector,
                                                             const ChunkModelOptions& options, std::string& error) {
    const int k = std::clamp(options.framesPerRun, 1, 16);
    if (dvector.empty()) {
        error = "no voice print yet (record your voice)";
        return nullptr;
    }
    if (impl_->voiceFilterBytes.empty() &&
        !readFile(impl_->modelDirectory.parent_path() / kVoiceFilterFile, impl_->voiceFilterBytes, error)) {
        return nullptr;
    }
    std::vector<char> bound;
    const std::vector<ConstantTensor> constants{{"dvector", {1, static_cast<std::int64_t>(dvector.size())}, dvector.data()}};
    if (!bindInputsAsConstants(impl_->voiceFilterBytes, constants, bound, error)) {
        error = "Cannot prepare voicefilter_lite.onnx: " + error;
        return nullptr;
    }
    try {
        Ort::SessionOptions so = baseOptions(options.threads, options.allowSpinning);
        so.AddFreeDimensionOverrideByName("S", chunkInputSamples(k));
        so.AddFreeDimensionOverrideByName("K", k);
        if (options.dmlDevice >= 0 && !appendDirectMl(so, options.dmlDevice, error)) {
            return nullptr;
        }
        Ort::Session session(*impl_->env, bound.data(), bound.size(), so);
        // The LSTM state shape comes from the model (h: (layers, 1, hidden)).
        std::array<std::int64_t, 3> stateShape{};
        bool found = false;
        Ort::AllocatorWithDefaultOptions allocator;
        for (std::size_t i = 0; i < session.GetInputCount(); ++i) {
            if (std::string(session.GetInputNameAllocated(i, allocator).get()) != "h") {
                continue;
            }
            const std::vector<std::int64_t> dims = session.GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape();
            if (dims.size() == 3 && dims[0] > 0 && dims[1] == 1 && dims[2] > 0) {
                stateShape = {dims[0], dims[1], dims[2]};
                found = true;
            }
        }
        if (!found || static_cast<std::size_t>(stateShape[0] * stateShape[2]) > kStateSize) {
            error = "voicefilter_lite.onnx has no usable state input h (layers, 1, hidden)";
            return nullptr;
        }
        return std::make_unique<OrtVoiceFilterModel>(std::move(session), k, stateShape);
    } catch (const std::exception& e) {
        error = std::string("Cannot load voicefilter_lite.onnx: ") + e.what();
        return nullptr;
    }
}

std::unique_ptr<ISpeakerModel> Runtime::createSpeakerModel(std::string& error) {
    std::vector<char> original;
    if (!readFile(impl_->modelDirectory / kSpeakerFile, original, error)) {
        return nullptr;
    }
    // CAM++ averages segments with AveragePool(kernel 100, ceil_mode=1, count_include_pad=1), trained in PyTorch,
    // which divides a shorter last segment by its real length. ONNX Runtime 1.24 divides it by 100 instead, and the
    // embeddings then hardly tell voices apart (a different speaker scored ~0.6 instead of ~0.0). Without padding
    // (pads are 0), count_include_pad=0 gives PyTorch's result on every ONNX Runtime version.
    std::vector<char> bytes;
    int changed = 0;
    if (!setNodeAttribute(original, "AveragePool", "count_include_pad", 0, bytes, changed, error)) {
        error = std::string("Cannot prepare ") + kSpeakerFile + ": " + error;
        return nullptr;
    }
    try {
        Ort::SessionOptions so;
        so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        so.SetIntraOpNumThreads(1); // a check every 0.3 s at most; keeps the cores for the isolation itself
        so.SetInterOpNumThreads(1);
        Ort::Session session(*impl_->env, bytes.data(), bytes.size(), so);
        return std::make_unique<OrtSpeakerModel>(std::move(session));
    } catch (const std::exception& e) {
        error = std::string("Cannot load ") + kSpeakerFile + ": " + e.what();
        return nullptr;
    }
}

std::string Runtime::description() const {
    return "ONNX Runtime " + ortProcess().version + (ortProcess().directMl ? " (CPU + DirectML)" : " (CPU)") +
           ", VoxLock INT8 Full";
}

bool Runtime::hasDirectMl() {
    return ortProcess().directMl;
}

} // namespace vs::voxlock
