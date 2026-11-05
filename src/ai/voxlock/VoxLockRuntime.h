#pragma once

#include "ai/voxlock/SpeakerCheck.h"
#include "ai/voxlock/VoxLockStream.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace vs::voxlock {

// Output of enroll.onnx for one 3 s voice sample.
struct EnrollmentTensors {
    std::vector<float> mag;                  // enroll_mag (1, 257, Fe)
    std::vector<std::int64_t> magDims;
    std::vector<float> context;              // context (1, 512, Fc)
    std::vector<std::int64_t> contextDims;
};

struct ChunkModelOptions {
    int framesPerRun = 2;   // K: hops per chunk.onnx call (8 ms each)
    int threads = 2;        // intra-op threads, including the calling thread
    bool allowSpinning = false; // pool threads sleep between chunks: ~0.4 instead of ~1 core, no measurable dropout difference
    int dmlDevice = -1;         // >= 0: run on this GPU with DirectML (its IDXGIFactory::EnumAdapters index); -1: CPU
};

// ONNX Runtime (CPU) + the VoxLock INT8 Full graphs. Non-real-time: every method may block for a while
// and must run off the audio threads. Only available when built with VOICESTUDIO_WITH_VOXLOCK.
class Runtime {
public:
    // Loads onnxruntime.dll from `dllDirectory` by full path (never a copy from the system directories) and
    // reads enroll.onnx and chunk_int8_full.onnx from `modelDirectory`. Returns nullptr with `error` set if
    // anything is missing or incompatible.
    [[nodiscard]] static std::unique_ptr<Runtime> create(const std::filesystem::path& dllDirectory,
                                                         const std::filesystem::path& modelDirectory, std::string& error);
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // Runs enroll.onnx on exactly kEnrollSamples samples (see fitSample()).
    bool enroll(const std::vector<float>& sample, EnrollmentTensors& out, std::string& error);

    // A chunk.onnx session bound to one enrollment, with a fixed K. Takes about a second (graph
    // optimisation folds the enrollment-only branch, see bindInputsAsConstants()).
    [[nodiscard]] std::unique_ptr<IChunkModel> createChunkModel(const EnrollmentTensors& enrollment,
                                                                const ChunkModelOptions& options, std::string& error);

    // The speaker-verification network (CAM++) for the voice check, from the same model directory. Its own
    // single-threaded CPU session; nullptr with `error` set if the file is missing or unusable.
    [[nodiscard]] std::unique_ptr<ISpeakerModel> createSpeakerModel(std::string& error);

    [[nodiscard]] std::string description() const;
    // This build and the loaded runtime can run models on GPUs (DirectML).
    [[nodiscard]] static bool hasDirectMl();

private:
    struct Impl;
    explicit Runtime(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace vs::voxlock
