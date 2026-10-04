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

1. 実機でPC実測値(htdemucs ONNX Runtime CPU EP: 推論120.69秒, End-to-End 156.67秒, 曲長240秒)と処理時間を比較
2. (将来のOSS化時)MP3等の他フォーマット対応、4音源すべての分離・書き出し
