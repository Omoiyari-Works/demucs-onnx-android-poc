#pragma once

#include <cstdint>

namespace demucspoc {

// These are fixed by the htdemucs ONNX graph itself (input shape is static,
// not dynamic), so they cannot be changed without re-exporting the model.
inline constexpr int kSampleRate = 44100;
inline constexpr int64_t kSegmentSamples = 343980;  // 7.8s at 44100Hz
inline constexpr int kChannelCount = 2;

// Quarter-segment overlap, matching demucs_onnx's inference.py.
inline constexpr int64_t kOverlapSamples = kSegmentSamples / 4;
inline constexpr int64_t kStrideSamples = kSegmentSamples - kOverlapSamples;

// Order of the model's "stems" output, matching demucs_onnx's SOURCES tuple.
enum class Stem : int64_t {
    kDrums = 0,
    kBass = 1,
    kOther = 2,
    kVocals = 3,
};

inline constexpr int64_t kStemCount = 4;

}  // namespace demucspoc
