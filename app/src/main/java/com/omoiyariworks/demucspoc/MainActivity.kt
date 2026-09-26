package com.omoiyariworks.demucspoc

import android.os.Bundle
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import kotlin.concurrent.thread

class MainActivity : AppCompatActivity() {

    private lateinit var statusText: TextView

    companion object {
        init {
            System.loadLibrary("demucspoc")
        }
    }

    private external fun nativeGetOrtVersion(): String

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        statusText = TextView(this).apply {
            text = "Tap the button to check native/ONNX Runtime linkage."
            textSize = 16f
            setPadding(32, 32, 32, 32)
        }
        val checkButton = Button(this).apply {
            text = "Check native link"
            setOnClickListener { runLinkCheck() }
        }

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
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
