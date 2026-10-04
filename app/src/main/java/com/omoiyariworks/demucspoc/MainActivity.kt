package com.omoiyariworks.demucspoc

import android.content.res.AssetManager
import android.graphics.Color
import android.media.MediaPlayer
import android.net.Uri
import android.os.Bundle
import android.util.Log
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import java.io.File
import kotlin.concurrent.thread

private const val LOG_TAG = "DemucsPoc"

class MainActivity : ComponentActivity() {

    private lateinit var statusText: TextView
    private lateinit var pickButton: Button
    private lateinit var playButton: Button
    private var lastOutputWavPath: String? = null

    companion object {
        private const val MODEL_ASSET_NAME = "htdemucs.onnx"
        private const val INPUT_WAV_FILE_NAME = "demucspoc_input.wav"
        private const val OUTPUT_WAV_FILE_NAME = "demucspoc_vocals.wav"

        // System.loadLibrary() here would run as a static initializer and, if it
        // fails, crash the app before onCreate() ever runs with no visible error.
        // Captured instead so onCreate() can show the failure on screen (there's
        // no adb/logcat access assumed for whoever is running this PoC build).
        private var nativeLoadError: Throwable? = null

        init {
            try {
                System.loadLibrary("demucspoc")
            } catch (e: Throwable) {
                nativeLoadError = e
            }
        }
    }

    private external fun nativeGetOrtVersion(): String

    // Returns [modelLoadMs, preprocessMs, inferenceMs, postprocessMs, endToEndMs].
    private external fun nativeSeparateVocals(
        assetManager: AssetManager,
        modelAssetName: String,
        inputWavPath: String,
        outputWavPath: String,
    ): LongArray

    private val pickAudioLauncher = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            runSeparation(uri)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        try {
            buildUi()
        } catch (e: Throwable) {
            // Bright, impossible-to-miss colors here: if THIS is what's on
            // screen, buildUi() itself is throwing (distinct from it silently
            // laying out invisible/zero-size views).
            setContentView(TextView(this).apply {
                text = "onCreate FAILED: ${e.javaClass.name}: ${e.message}"
                textSize = 20f
                setTextColor(Color.WHITE)
                setBackgroundColor(Color.RED)
                setPadding(32, 32, 32, 32)
            })
        }
    }

    private fun buildUi() {
        val loadError = nativeLoadError
        statusText = TextView(this).apply {
            text = if (loadError != null) {
                "native library load FAILED: ${loadError.javaClass.name}: ${loadError.message}"
            } else {
                "Tap \"Check native link\" to verify ONNX Runtime linkage, or pick a WAV" +
                    " (44100Hz/stereo/16-bit PCM) to separate vocals."
            }
            textSize = 20f
            setTextColor(Color.WHITE)
            setBackgroundColor(Color.BLUE)
            setPadding(32, 32, 32, 32)
        }
        val checkButton = Button(this).apply {
            text = "Check native link"
            setTextColor(Color.BLACK)
            setBackgroundColor(Color.YELLOW)
            isEnabled = loadError == null
            setOnClickListener { runLinkCheck() }
        }
        pickButton = Button(this).apply {
            text = "Pick WAV & separate vocals"
            setTextColor(Color.BLACK)
            setBackgroundColor(Color.YELLOW)
            isEnabled = loadError == null
            setOnClickListener { pickAudioLauncher.launch(arrayOf("audio/*")) }
        }
        playButton = Button(this).apply {
            text = "Play separated vocals"
            setTextColor(Color.BLACK)
            setBackgroundColor(Color.YELLOW)
            isEnabled = false
            setOnClickListener { playLastOutput() }
        }

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT,
            )
            setBackgroundColor(Color.GREEN)
            addView(
                statusText,
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                ),
            )
            addView(
                checkButton,
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                ),
            )
            addView(
                pickButton,
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                ),
            )
            addView(
                playButton,
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                ),
            )
        }

        // targetSdk 36 draws content edge-to-edge with no way to opt back out
        // (setDecorFitsSystemWindows(true) didn't help: status text/button were
        // still rendered under the status bar). Padding the root by the actual
        // system bar insets keeps content clear of them instead.
        ViewCompat.setOnApplyWindowInsetsListener(root) { v, insets ->
            val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
            v.setPadding(bars.left, bars.top, bars.right, bars.bottom)
            insets
        }

        setContentView(root)
    }

    private fun runLinkCheck() {
        statusText.text = "Checking..."
        thread {
            val result = try {
                "native lib OK, ONNX Runtime version: ${nativeGetOrtVersion()}"
            } catch (e: Throwable) {
                "native link check FAILED: ${e.message}"
            }
            runOnUiThread { statusText.text = result }
        }
    }

    private fun runSeparation(sourceUri: Uri) {
        statusText.text = "Copying selected file..."
        pickButton.isEnabled = false
        playButton.isEnabled = false

        thread {
            val startMs = System.currentTimeMillis()
            val result = try {
                val inputWavFile = File(cacheDir, INPUT_WAV_FILE_NAME)
                contentResolver.openInputStream(sourceUri)?.use { input ->
                    inputWavFile.outputStream().use { output -> input.copyTo(output) }
                } ?: throw IllegalStateException("could not open selected file")
                val copyMs = System.currentTimeMillis() - startMs

                val outputWavFile = File(cacheDir, OUTPUT_WAV_FILE_NAME)
                val timings = nativeSeparateVocals(
                    assets,
                    MODEL_ASSET_NAME,
                    inputWavFile.absolutePath,
                    outputWavFile.absolutePath,
                )
                val totalMs = System.currentTimeMillis() - startMs
                lastOutputWavPath = outputWavFile.absolutePath

                val summary = "copy=${copyMs}ms model_load=${timings[0]}ms preprocess=${timings[1]}ms " +
                    "inference=${timings[2]}ms postprocess=${timings[3]}ms native_e2e=${timings[4]}ms " +
                    "total(incl. copy)=${totalMs}ms"
                Log.i(LOG_TAG, "separation timings: $summary output=${outputWavFile.absolutePath}")

                "Done.\n$summary\noutput=${outputWavFile.absolutePath}"
            } catch (e: Throwable) {
                Log.e(LOG_TAG, "separation failed", e)
                "Separation FAILED: ${e.javaClass.name}: ${e.message}"
            }
            runOnUiThread {
                statusText.text = result
                pickButton.isEnabled = true
                playButton.isEnabled = lastOutputWavPath != null
            }
        }
    }

    private fun playLastOutput() {
        val path = lastOutputWavPath ?: return
        try {
            MediaPlayer().apply {
                setDataSource(path)
                prepare()
                start()
                setOnCompletionListener { release() }
            }
        } catch (e: Throwable) {
            statusText.text = "Playback FAILED: ${e.message}"
        }
    }
}
