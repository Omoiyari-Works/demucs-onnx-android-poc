# Demucs ONNX Android PoC

## Investigation Summary

This PoC was built to investigate how to implement instrumental/vocal separation in [FlashMashApp](https://github.com/Omoiyari-Works/FlashMashApp) (issue [#486](https://github.com/Omoiyari-Works/FlashMashApp/issues/486)). Findings below.

### Model selection

Compared Demucs (htdemucs) / Spleeter / Open-Unmix (UMX) / MDX-Net family on PC:

- **Demucs (htdemucs)**: 81MB model size (measured), waveform-domain end-to-end processing, actively maintained (17 PyPI releases from April 2021 to July 2026)
- **Open-Unmix (UMX)**: 432MB model size, last released April 2024 (~2.5 years without updates)
- UMX was faster in some tests, but given the maintenance gap, Demucs was chosen as the primary candidate

### ONNX export

The official Demucs repository does not officially support ONNX export. Plain `torch.onnx.export` hit two blockers:

- Legacy exporter: `th.stft(..., return_complex=True)` inside `spectro()` returns a complex tensor, which ONNX's STFT operator doesn't support
- New dynamo exporter: an input-shape-dependent branch (`if mix.shape[-1] < training_length:`) can't be resolved into a static graph

Both are solved by the third-party package [`demucs-onnx`](https://github.com/StemSplit/demucs-onnx) (replaces the complex STFT with a real/imaginary split via Conv1d), which this PoC uses for exporting `htdemucs.onnx`. PyTorch↔ONNX numerical parity: max abs diff 0.000669.

### Quantization (tried twice, rejected both times)

- PyTorch eager-mode dynamic quantization (`torch.quantization.quantize_dynamic`) on Open-Unmix: crashed with an OOM kill (~14GB memory usage) before speed could even be measured
- ONNX Runtime dynamic quantization (`quantize_dynamic`, INT8) on the Demucs ONNX model: model size dropped 316MB → 197MB (-38%), but inference got **1.8x slower** on PC, and the output's numerical difference from fp32 reached ~50% of the original signal's RMS (audibly degraded)

Conclusion: quantization is not a viable speedup path for this model/library combination, at least with these straightforward approaches.

### On-device vs PC timing (1-minute WAV, 11 chunks)

| | PC (Xeon, CPU EP) | Android device (CPU EP) |
|---|---|---|
| Model load | 48.18s | 11.38s |
| Inference only | 47.60s (RTF 0.79) | 131.10s (RTF 2.19) |
| End-to-end | 96.25s | 144.82s |

RTF (Real-Time Factor) is processing time divided by audio duration. The device's RTF of 2.19 means processing takes over twice as long as the song itself — currently slower than real-time. Extrapolated to a full ~4-minute song, end-to-end time is expected to be around 8 minutes on-device.

### Saving output files visibly to the user (for the real FlashMashApp integration)

This PoC writes output to the app's private `cacheDir` only. For the real implementation (JUCE-based), no custom JNI/MediaStore code is needed — `juce::FileChooser` + `juce::AndroidDocument` already cover this:

- `FileChooser` in save mode launches Android's native SAF (`ACTION_CREATE_DOCUMENT`) save dialog — Scoped Storage compliant out of the box. FlashMashApp already has a reference implementation in `Source/Presenter/ProjectArchivePresenter.cpp` (`launchSaveFileChooser()` / `openOutputStreamForUrl()`).
- For saving two files (instrumental + vocals) into one user-picked folder: launch `FileChooser` in directory-select mode (`canSelectDirectories`, i.e. `ACTION_OPEN_DOCUMENT_TREE`) once, then use `juce::AndroidDocument::fromTree(url)` and `createChildDocumentWithTypeAndName("audio/wav", name)` to create each file inside that folder. `AndroidDocumentPermission::takePersistentReadWriteAccess()` can persist folder access across app restarts so the user doesn't have to re-pick it every time.
