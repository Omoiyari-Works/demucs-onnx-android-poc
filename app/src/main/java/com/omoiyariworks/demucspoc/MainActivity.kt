package com.omoiyariworks.demucspoc

import android.app.Activity
import android.graphics.Color
import android.os.Bundle
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
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
        try {
            buildUi()
        } catch (e: Throwable) {
            // Same reasoning as nativeLoadError below: nobody running this PoC
            // build is assumed to have adb/logcat access, so surface it on screen.
            setContentView(TextView(this).apply {
                text = "onCreate FAILED: ${e.javaClass.name}: ${e.message}"
                textSize = 16f
                setPadding(32, 32, 32, 32)
            })
        }
    }

    private fun buildUi() {
        // Text/background colors set explicitly rather than left to the theme:
        // system dark mode can force a light theme's default text color to
        // white, leaving text present but invisible against a white background.
        val loadError = nativeLoadError
        statusText = TextView(this).apply {
            text = if (loadError != null) {
                "native library load FAILED: ${loadError.javaClass.name}: ${loadError.message}"
            } else {
                "Tap the button to check native/ONNX Runtime linkage."
            }
            textSize = 16f
            setTextColor(Color.BLACK)
            setPadding(32, 32, 32, 32)
        }
        val checkButton = Button(this).apply {
            text = "Check native link"
            setTextColor(Color.BLACK)
            isEnabled = loadError == null
            setOnClickListener { runLinkCheck() }
        }

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.WHITE)
            addView(statusText)
            addView(checkButton)
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
