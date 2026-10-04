#pragma once

#include <android/asset_manager.h>

#include <cstdint>
#include <string>

namespace demucspoc {

struct SeparationTimings {
    int64_t modelLoadMs = 0;
    int64_t preprocessMs = 0;
    int64_t inferenceMs = 0;    // sum of ONNX Runtime session.Run() calls only
    int64_t postprocessMs = 0;  // tensor setup, overlap-add, normalization, WAV write
    int64_t endToEndMs = 0;
};

class DemucsSeparator {
public:
    // Loads `modelAssetName` from `assetManager`, reads `inputWavPath` (must be
    // 44100Hz/stereo/16-bit PCM), runs htdemucs in fixed-length chunks, and
    // writes the separated vocals stem to `outputWavPath`.
    //
    // Throws std::runtime_error or Ort::Exception (both derive from
    // std::exception) on failure. This class does not catch anything itself;
    // the JNI boundary (native-lib.cpp) is responsible for catching and
    // converting to a Java exception.
    SeparationTimings separateVocals(AAssetManager* assetManager, const std::string& modelAssetName,
                                      const std::string& inputWavPath, const std::string& outputWavPath);
};

}  // namespace demucspoc
