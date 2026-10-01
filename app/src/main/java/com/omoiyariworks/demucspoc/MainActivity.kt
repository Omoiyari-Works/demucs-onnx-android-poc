package com.omoiyariworks.demucspoc

import android.app.Activity
import android.graphics.Color
import android.os.Bundle
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.core.view.WindowCompat
import kotlin.concurrent.thread

class MainActivity : Activity() {

    private lateinit var statusText: TextView

    companion object {
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

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // Android 15+ (targetSdk 35+) draws app content edge-to-edge by default,
        // under the status/navigation bars. Without inset handling, top-aligned
        // views (statusText here) end up rendered behind the status bar —
        // consistent with "only the title area is visible, green background
        // shows, but the blue status text never appeared". Opting back into the
        // pre-edge-to-edge layout behavior for this PoC rather than adding
        // inset padding everywhere.
        WindowCompat.setDecorFitsSystemWindows(window, true)
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
        // Bright, distinct colors + explicit MATCH_PARENT sizing to rule out
        // both "text color same as background" and "zero-size layout" as
        // causes of a blank screen, now that black-on-white didn't help.
        val loadError = nativeLoadError
        statusText = TextView(this).apply {
            text = if (loadError != null) {
                "native library load FAILED: ${loadError.javaClass.name}: ${loadError.message}"
            } else {
                "Tap the button to check native/ONNX Runtime linkage."
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
}
