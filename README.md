# Demucs ONNX Android PoC

FlashMashApp本体へのDemucs(音源分離)組み込み前に、Android実機上でONNX Runtimeを使った推論がどの程度の速度で動くかを検証するための最小構成アプリ。

## 現在の状態

- Kotlin UI + C++(JNI)からONNX Runtime C++ APIを呼び出す最小疎通確認(フェーズ4)まで完了
- 実際の音源分離処理(モデル読み込み・segment分割・推論)は未実装

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

1. `htdemucs.onnx`をassetsに配置し、C++側でONNX Runtimeセッションを初期化してダミー推論を通す
2. `demucs_onnx`(Python版、`inference.py`)のsegment分割・overlap-addロジックをC++に移植
3. 実際の音声ファイルを分離し、モデルロード時間/推論時間/全体時間をLogcatに出力
4. PC実測値(htdemucs ONNX Runtime CPU EP: 推論120.69秒, End-to-End 156.67秒, 曲長240秒)との比較
