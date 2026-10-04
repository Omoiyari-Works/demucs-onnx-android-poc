#pragma once

#include <string>
#include <vector>

namespace demucspoc {

// Planar (non-interleaved) stereo waveform, one float per sample in [-1, 1].
struct StereoWaveform {
    std::vector<float> left;
    std::vector<float> right;
};

// Reads a WAV file that must be 44100Hz / stereo / 16-bit PCM.
// Throws std::runtime_error if the file cannot be opened or does not match
// that exact format (this is a fixed-format reader, not a general WAV parser).
StereoWaveform readFixedFormatWav(const std::string& path);

// Writes `left`/`right` (must be equal length) as a 44100Hz / stereo / 16-bit
// PCM WAV file. Samples are clamped to [-1, 1] before int16 conversion.
// Throws std::runtime_error if the file cannot be created.
void writeFixedFormatWav(const std::string& path, const std::vector<float>& left,
                          const std::vector<float>& right);

}  // namespace demucspoc
