#include "demucs_separator.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <vector>

#include "demucs_constants.h"
#include "wav_io.h"

namespace demucspoc {

namespace {

using Clock = std::chrono::steady_clock;

int64_t elapsedMs(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

// Trapezoidal cross-fade window matching demucs_onnx's `_make_transition_window`:
// linear fade-in over the first kOverlapSamples, flat at 1.0 in the middle,
// linear fade-out over the last kOverlapSamples. (Despite being described as
// "triangular" in demucs_onnx's own docstring, it is a trapezoid: see the
// investigation notes in the project plan.)
std::vector<float> makeTransitionWindow() {
    std::vector<float> window(static_cast<size_t>(kSegmentSamples), 1.0f);
    for (int64_t i = 0; i < kOverlapSamples; ++i) {
        const float value = static_cast<float>(i) / static_cast<float>(kOverlapSamples - 1);
        window[static_cast<size_t>(i)] = value;
        window[static_cast<size_t>(kSegmentSamples - 1 - i)] = value;
    }
    return window;
}

}  // namespace

SeparationTimings DemucsSeparator::separateVocals(AAssetManager* assetManager,
                                                    const std::string& modelAssetName,
                                                    const std::string& inputWavPath,
                                                    const std::string& outputWavPath) {
    const auto startTotal = Clock::now();
    SeparationTimings timings;

    // 1. Load the model from assets and create the ONNX Runtime session.
    const auto startLoad = Clock::now();
    const std::unique_ptr<AAsset, decltype(&AAsset_close)> modelAsset(
        AAssetManager_open(assetManager, modelAssetName.c_str(), AASSET_MODE_BUFFER), &AAsset_close);
    if (!modelAsset) {
        throw std::runtime_error("DemucsSeparator: failed to open model asset: " + modelAssetName);
    }
    const void* modelData = AAsset_getBuffer(modelAsset.get());
    const off64_t modelLength = AAsset_getLength64(modelAsset.get());
    if (modelData == nullptr || modelLength <= 0) {
        throw std::runtime_error("DemucsSeparator: failed to read model asset buffer: " + modelAssetName);
    }

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "demucspoc");
    Ort::SessionOptions sessionOptions;
    sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    Ort::Session session(env, modelData, static_cast<size_t>(modelLength), sessionOptions);

    if (session.GetInputCount() != 1 || session.GetOutputCount() != 1) {
        throw std::runtime_error("DemucsSeparator: unexpected model input/output count");
    }
    Ort::AllocatorWithDefaultOptions allocator;
    const Ort::AllocatedStringPtr inputNameHolder = session.GetInputNameAllocated(0, allocator);
    const Ort::AllocatedStringPtr outputNameHolder = session.GetOutputNameAllocated(0, allocator);
    const std::string inputName = inputNameHolder.get();
    const std::string outputName = outputNameHolder.get();

    timings.modelLoadMs = elapsedMs(startLoad, Clock::now());

    // 2. Read the input WAV (already normalized to 44100Hz/stereo/16-bit by the
    // caller before this function is invoked).
    const auto startPreprocess = Clock::now();
    const StereoWaveform input = readFixedFormatWav(inputWavPath);
    const auto totalLen = static_cast<int64_t>(input.left.size());
    if (totalLen == 0) {
        throw std::runtime_error("DemucsSeparator: input WAV has no samples: " + inputWavPath);
    }

    const std::vector<float> window = makeTransitionWindow();
    std::vector<float> vocalsLeft(static_cast<size_t>(totalLen), 0.0f);
    std::vector<float> vocalsRight(static_cast<size_t>(totalLen), 0.0f);
    std::vector<float> weight(static_cast<size_t>(totalLen), 0.0f);

    const int64_t nChunks = (totalLen + kStrideSamples - 1) / kStrideSamples;

    // Reused across chunks: (1, 2, kSegmentSamples) planar input, zero-filled
    // then overwritten per chunk so the tail padding of the final chunk is
    // implicit rather than a special case.
    std::vector<float> chunkInput(static_cast<size_t>(kChannelCount) * static_cast<size_t>(kSegmentSamples),
                                   0.0f);
    const std::array<int64_t, 3> inputShape{1, kChannelCount, kSegmentSamples};
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    timings.preprocessMs = elapsedMs(startPreprocess, Clock::now());

    // 3. Chunked inference with weighted overlap-add, accumulating the vocals
    // stem only (the other 3 stems are discarded; this PoC only needs vocals
    // for a listening sanity-check).
    const auto startPostLoop = Clock::now();
    int64_t inferenceMsAccum = 0;

    const char* inputNames[] = {inputName.c_str()};
    const char* outputNames[] = {outputName.c_str()};
    const int64_t stemStride = static_cast<int64_t>(kChannelCount) * kSegmentSamples;

    for (int64_t chunkIndex = 0; chunkIndex < nChunks; ++chunkIndex) {
        const int64_t start = chunkIndex * kStrideSamples;
        const int64_t end = std::min(start + kSegmentSamples, totalLen);
        const int64_t chunkLen = end - start;

        std::fill(chunkInput.begin(), chunkInput.end(), 0.0f);
        std::copy(input.left.begin() + start, input.left.begin() + end, chunkInput.begin());
        std::copy(input.right.begin() + start, input.right.begin() + end,
                  chunkInput.begin() + kSegmentSamples);

        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo, chunkInput.data(), chunkInput.size(), inputShape.data(), inputShape.size());

        const auto startInference = Clock::now();
        std::vector<Ort::Value> outputTensors =
            session.Run(Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);
        inferenceMsAccum += elapsedMs(startInference, Clock::now());

        const float* stemsData = outputTensors[0].GetTensorData<float>();
        const float* vocalsBase = stemsData + static_cast<int64_t>(Stem::kVocals) * stemStride;
        const float* vocalsLeftChunk = vocalsBase;
        const float* vocalsRightChunk = vocalsBase + kSegmentSamples;

        for (int64_t i = 0; i < chunkLen; ++i) {
            const float w = window[static_cast<size_t>(i)];
            vocalsLeft[static_cast<size_t>(start + i)] += vocalsLeftChunk[i] * w;
            vocalsRight[static_cast<size_t>(start + i)] += vocalsRightChunk[i] * w;
            weight[static_cast<size_t>(start + i)] += w;
        }
    }

    // 4. Normalize by accumulated window weight and write the vocals stem.
    constexpr float kMinWeight = 1e-8f;
    for (int64_t i = 0; i < totalLen; ++i) {
        const float w = std::max(weight[static_cast<size_t>(i)], kMinWeight);
        vocalsLeft[static_cast<size_t>(i)] /= w;
        vocalsRight[static_cast<size_t>(i)] /= w;
    }
    writeFixedFormatWav(outputWavPath, vocalsLeft, vocalsRight);

    const auto endAll = Clock::now();
    timings.inferenceMs = inferenceMsAccum;
    timings.postprocessMs = elapsedMs(startPostLoop, endAll) - inferenceMsAccum;
    timings.endToEndMs = elapsedMs(startTotal, endAll);
    return timings;
}

}  // namespace demucspoc
