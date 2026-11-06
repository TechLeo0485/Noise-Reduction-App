#include "audio/isolation/IsolationModels.h"

namespace vs {

namespace {
constexpr const char* kSpeakerCheckFile = "voxlock/3dspeaker_speech_campplus_sv_en_voxceleb_16k.onnx";
constexpr const char* kEnrollFile = "voxlock/enroll.onnx";
} // namespace

const std::vector<IsolationModelInfo>& isolationModels() {
    static const std::vector<IsolationModelInfo> models = {
        {"voxlock",
         "Full isolation",
         "VoxLock neural network",
         "Listens to the voice you recorded and rebuilds only your voice from the microphone. Other people and most "
         "noise are removed - even while you are talking.",
         "Calls and interviews where others talk near you. The best choice if your computer keeps up.",
         "RECOMMENDED",
         "~80 ms",
         "Medium",
         "Removed or turned down",
         {kEnrollFile, "voxlock/chunk_int8_full.onnx", kSpeakerCheckFile},
         "The VoxLock model files are missing from the models\\voxlock folder."},
        {"voicefilter",
         "Lightweight filter",
         "VoiceFilter-Lite design (Google), trained for VoiceStudio",
         "A small network compares every moment of sound with your voice print and turns down whatever is not you. "
         "Lighter than Full isolation, but it separates overlapping voices less cleanly.",
         "Laptops that are too slow for Full isolation but should still turn down voices while you talk.",
         "EXPERIMENTAL",
         "~70 ms",
         "Low",
         "Turned down",
         {kEnrollFile, kSpeakerCheckFile, "voicefilter/voicefilter_lite.onnx"},
         "Needs a trained model: models\\voicefilter\\voicefilter_lite.onnx. Google did not publish one - train it "
         "with tools\\voicefilter (see its README)."},
        {"light",
         "Voice check only",
         "Speaker check (CAM++), no separation",
         "Recognises whether the voice is yours. While someone else talks and you are silent, your microphone is "
         "muted. Nothing is removed while you talk; steady background noise is.",
         "Very slow computers, or rooms where others rarely talk at the same moment as you.",
         "LOWEST CPU",
         "~70 ms",
         "Very low",
         "Passed along with your voice",
         {kEnrollFile, kSpeakerCheckFile},
         "The speaker-check model is missing from the models\\voxlock folder."},
    };
    return models;
}

const IsolationModelInfo* findIsolationModel(std::string_view id) {
    for (const IsolationModelInfo& model : isolationModels()) {
        if (model.id == id) {
            return &model;
        }
    }
    return nullptr;
}

bool isolationModelInstalled(const IsolationModelInfo& model, const std::filesystem::path& modelsDir) {
    for (const std::string& file : model.files) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(modelsDir / std::filesystem::path(file), ec)) {
            return false;
        }
    }
    return true;
}

} // namespace vs
