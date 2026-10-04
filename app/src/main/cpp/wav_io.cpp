#include "wav_io.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace demucspoc {

namespace {

constexpr int kExpectedSampleRate = 44100;
constexpr uint16_t kExpectedChannels = 2;
constexpr uint16_t kExpectedBitsPerSample = 16;
constexpr uint16_t kPcmAudioFormat = 1;
constexpr float kInt16Scale = 32768.0f;

uint32_t readLE32(const unsigned char* bytes) {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

uint16_t readLE16(const unsigned char* bytes) {
    return static_cast<uint16_t>(bytes[0]) | static_cast<uint16_t>(bytes[1] << 8);
}

void writeLE32(std::ofstream& out, uint32_t value) {
    const std::array<unsigned char, 4> bytes{
        static_cast<unsigned char>(value & 0xffU),
        static_cast<unsigned char>((value >> 8) & 0xffU),
        static_cast<unsigned char>((value >> 16) & 0xffU),
        static_cast<unsigned char>((value >> 24) & 0xffU),
    };
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void writeLE16(std::ofstream& out, uint16_t value) {
    const std::array<unsigned char, 2> bytes{
        static_cast<unsigned char>(value & 0xffU),
        static_cast<unsigned char>((value >> 8) & 0xffU),
    };
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

StereoWaveform readFixedFormatWav(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("wav_io: failed to open input file: " + path);
    }

    std::array<unsigned char, 12> riffHeader{};
    file.read(reinterpret_cast<char*>(riffHeader.data()), static_cast<std::streamsize>(riffHeader.size()));
    if (!file || std::memcmp(riffHeader.data(), "RIFF", 4) != 0 ||
        std::memcmp(riffHeader.data() + 8, "WAVE", 4) != 0) {
        throw std::runtime_error("wav_io: not a RIFF/WAVE file: " + path);
    }

    bool haveFmt = false;
    bool haveData = false;
    uint16_t audioFormat = 0;
    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    std::vector<unsigned char> dataBytes;

    std::array<unsigned char, 8> chunkHeader{};
    while (file.read(reinterpret_cast<char*>(chunkHeader.data()), static_cast<std::streamsize>(chunkHeader.size()))) {
        const uint32_t chunkSize = readLE32(chunkHeader.data() + 4);

        if (std::memcmp(chunkHeader.data(), "fmt ", 4) == 0) {
            if (chunkSize < 16) {
                throw std::runtime_error("wav_io: truncated fmt chunk: " + path);
            }
            std::vector<unsigned char> fmtBytes(chunkSize);
            file.read(reinterpret_cast<char*>(fmtBytes.data()), static_cast<std::streamsize>(chunkSize));
            if (!file) {
                throw std::runtime_error("wav_io: truncated fmt chunk: " + path);
            }
            audioFormat = readLE16(fmtBytes.data());
            numChannels = readLE16(fmtBytes.data() + 2);
            sampleRate = readLE32(fmtBytes.data() + 4);
            bitsPerSample = readLE16(fmtBytes.data() + 14);
            haveFmt = true;
        } else if (std::memcmp(chunkHeader.data(), "data", 4) == 0) {
            dataBytes.resize(chunkSize);
            file.read(reinterpret_cast<char*>(dataBytes.data()), static_cast<std::streamsize>(chunkSize));
            if (!file) {
                throw std::runtime_error("wav_io: truncated data chunk: " + path);
            }
            haveData = true;
        } else {
            file.seekg(chunkSize, std::ios::cur);
        }
        if (chunkSize % 2 != 0) {
            file.seekg(1, std::ios::cur);  // chunks are padded to even sizes
        }
        if (haveFmt && haveData) {
            break;
        }
    }

    if (!haveFmt || !haveData) {
        throw std::runtime_error("wav_io: missing fmt or data chunk: " + path);
    }
    if (audioFormat != kPcmAudioFormat || numChannels != kExpectedChannels ||
        sampleRate != static_cast<uint32_t>(kExpectedSampleRate) || bitsPerSample != kExpectedBitsPerSample) {
        throw std::runtime_error(
            "wav_io: unsupported format (expected 16-bit PCM, " + std::to_string(kExpectedSampleRate) +
            "Hz, " + std::to_string(kExpectedChannels) + "ch; got format=" + std::to_string(audioFormat) +
            " channels=" + std::to_string(numChannels) + " sampleRate=" + std::to_string(sampleRate) +
            " bitsPerSample=" + std::to_string(bitsPerSample) + "): " + path);
    }

    const size_t bytesPerFrame = static_cast<size_t>(kExpectedChannels) * sizeof(int16_t);
    const size_t frameCount = dataBytes.size() / bytesPerFrame;

    StereoWaveform waveform;
    waveform.left.resize(frameCount);
    waveform.right.resize(frameCount);

    for (size_t i = 0; i < frameCount; ++i) {
        const unsigned char* frame = dataBytes.data() + i * bytesPerFrame;
        const auto leftSample = static_cast<int16_t>(readLE16(frame));
        const auto rightSample = static_cast<int16_t>(readLE16(frame + sizeof(int16_t)));
        waveform.left[i] = static_cast<float>(leftSample) / kInt16Scale;
        waveform.right[i] = static_cast<float>(rightSample) / kInt16Scale;
    }
    return waveform;
}

void writeFixedFormatWav(const std::string& path, const std::vector<float>& left,
                          const std::vector<float>& right) {
    if (left.size() != right.size()) {
        throw std::runtime_error("wav_io: left/right channel length mismatch");
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("wav_io: failed to create output file: " + path);
    }

    const auto frameCount = static_cast<uint32_t>(left.size());
    constexpr uint16_t bitsPerSample = kExpectedBitsPerSample;
    constexpr uint16_t numChannels = kExpectedChannels;
    constexpr auto sampleRate = static_cast<uint32_t>(kExpectedSampleRate);
    const auto blockAlign = static_cast<uint16_t>(numChannels * (bitsPerSample / 8));
    const uint32_t byteRate = sampleRate * blockAlign;
    const uint32_t dataSize = frameCount * blockAlign;

    file.write("RIFF", 4);
    writeLE32(file, 36 + dataSize);
    file.write("WAVE", 4);

    file.write("fmt ", 4);
    writeLE32(file, 16);
    writeLE16(file, kPcmAudioFormat);
    writeLE16(file, numChannels);
    writeLE32(file, sampleRate);
    writeLE32(file, byteRate);
    writeLE16(file, blockAlign);
    writeLE16(file, bitsPerSample);

    file.write("data", 4);
    writeLE32(file, dataSize);

    for (uint32_t i = 0; i < frameCount; ++i) {
        const float clampedLeft = std::clamp(left[i], -1.0f, 1.0f);
        const float clampedRight = std::clamp(right[i], -1.0f, 1.0f);
        writeLE16(file, static_cast<uint16_t>(static_cast<int16_t>(clampedLeft * kInt16Scale)));
        writeLE16(file, static_cast<uint16_t>(static_cast<int16_t>(clampedRight * kInt16Scale)));
    }

    if (!file) {
        throw std::runtime_error("wav_io: write failed: " + path);
    }
}

}  // namespace demucspoc
