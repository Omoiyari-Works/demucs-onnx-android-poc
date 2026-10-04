# Demucs ONNX Android PoC

FlashMashApp本体へのDemucs(音源分離)組み込み前に、Android実機上でONNX Runtimeを使った推論がどの程度の速度で動くかを検証するための最小構成アプリ。

## 現在の状態

- Kotlin UI + C++(JNI)からONNX Runtime C++ APIを呼び出す最小疎通確認(フェーズ4)完了
- 実際の音源分離処理(モデル読み込み・segment分割・推論・overlap-add・vocals書き出し)まで実装済み(フェーズ5)
- 使い方: アプリ内「Pick WAV & separate vocals」ボタンで44.1kHz/ステレオ/16bit PCMのWAVファイルを選択すると、vocalsのみ分離してWAV書き出しし、「Play separated vocals」ボタンで再生確認できる。処理時間(モデルロード/前処理/推論/後処理/End-to-End)は画面とLogcat(タグ`DemucsPoc`)に出力される
- MP3等の他フォーマットには対応していない(検証優先のため今回はスコープ外。将来のOSS化時に追加予定)

## モデルファイルについて

学習済みONNXモデル(`htdemucs.onnx`, 約300MB)はサイズが大きいため、このリポジトリには含めていない。開発時に以下の手順でローカル生成する。

```bash
pip install 'demucs-onnx[export]'
demucs-onnx export htdemucs app/src/main/assets/htdemucs.onnx
```

CIでも同じ手順でモデルを生成してからビルドする(`.github/workflows/build-apk.yml`、Actionsタブから手動トリガー、ビルド結果はWorkflow run artifactとしてダウンロード可能)。モデルファイル自体はリポジトリにコミットしていない。

内部で公式のPyTorchチェックポイント(Meta配布)をダウンロードし、STFT等の互換パッチを当てた上でONNXへ変換する(エクスポート成功済み、PyTorch↔ONNX数値差はmax abs diff 0.000669で検証済み)。

## ビルド方法

```bash
export ANDROID_HOME=/path/to/android-sdk
./gradlew assembleDebug
```

必要なSDKコンポーネント: `platforms;android-36`, `build-tools;36.0.0`, `ndk;29.0.14206865`, `cmake;4.1.2`(FlashMashApp本体と同じNDK/CMakeバージョンに揃えている)。

ONNX Runtime Android AAR(`com.microsoft.onnxruntime:onnxruntime-android`)はprefabパッケージを提供していないため、`app/build.gradle`の`extractOnnxRuntimeNative`タスクでAARからヘッダーと`.so`を展開し、CMakeに`ONNXRUNTIME_DIR`として渡している。

デバッグAPKは`arm64-v8a`のみに絞っている(実機はほぼ全てarm64-v8aのため)。

## 今後の予定

1. ~~実機でPC実測値(htdemucs ONNX Runtime CPU EP: 推論120.69秒, End-to-End 156.67秒, 曲長240秒)と処理時間を比較~~ → 完了(下記Investigation Summary参照)
2. (将来のOSS化時)MP3等の他フォーマット対応、4音源すべての分離・書き出し

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

### Next steps for production

- Speed: still needs further optimization (XNNPACK EP, GPU delegate, or a lighter model) since quantization didn't help
- UX: an 8-minute wait for a 4-minute song needs background processing, progress indication (per-chunk progress is available), and a completion notification — not a "convert and wait" flow
- Scope: this PoC only extracts the vocals stem and only accepts WAV input; production needs all 4 stems and additional format support (e.g. MP3, via Android's `MediaExtractor`/`MediaCodec`)
