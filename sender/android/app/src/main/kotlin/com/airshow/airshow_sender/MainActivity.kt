package com.airshow.airshow_sender

import android.app.Activity
import android.content.Intent
import android.media.projection.MediaProjectionManager
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    companion object {
        const val METHOD_CHANNEL = "com.airshow/capture"
        const val EVENT_CHANNEL = "com.airshow/capture_events"

        // Shared event sink for the capture service to post events back to Dart
        var eventSink: EventChannel.EventSink? = null
    }

    private var pendingHost: String = ""
    private var pendingPort: Int = 7400

    private val projectionResult = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK && result.data != null) {
            val serviceIntent = Intent(this, AirShowCaptureService::class.java).apply {
                putExtra("resultCode", result.resultCode)
                putExtra("data", result.data)
                putExtra("host", pendingHost)
                putExtra("port", pendingPort)
            }
            ContextCompat.startForegroundService(this, serviceIntent)
        } else {
            eventSink?.success(mapOf("type" to "ERROR", "message" to "Screen capture permission denied"))
        }
    }

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)

        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, METHOD_CHANNEL)
            .setMethodCallHandler { call, result ->
                when (call.method) {
                    "startCapture" -> {
                        pendingHost = call.argument<String>("host") ?: ""
                        pendingPort = call.argument<Int>("port") ?: 7400
                        val manager = getSystemService(MediaProjectionManager::class.java)
                        projectionResult.launch(manager.createScreenCaptureIntent())
                        result.success(null)
                    }
                    "stopCapture" -> {
                        val stopIntent = Intent(this, AirShowCaptureService::class.java)
                        stopIntent.action = "STOP"
                        startService(stopIntent)
                        result.success(null)
                    }
                    else -> result.notImplemented()
                }
            }

        EventChannel(flutterEngine.dartExecutor.binaryMessenger, EVENT_CHANNEL)
            .setStreamHandler(object : EventChannel.StreamHandler {
                override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
                    eventSink = events
                }
                override fun onCancel(arguments: Any?) {
                    eventSink = null
                }
            })
    }
}
