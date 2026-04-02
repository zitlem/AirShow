package com.airshow.airshow_sender

import android.app.Activity
import android.content.Intent
import android.media.projection.MediaProjectionManager
import androidx.core.content.ContextCompat
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    companion object {
        const val METHOD_CHANNEL = "com.airshow/capture"
        const val EVENT_CHANNEL = "com.airshow/capture_events"
        private const val REQUEST_MEDIA_PROJECTION = 1001

        // Shared event sink for the capture service to post events back to Dart
        var eventSink: EventChannel.EventSink? = null
    }

    private var pendingHost: String = ""
    private var pendingPort: Int = 7400

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)

        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, METHOD_CHANNEL)
            .setMethodCallHandler { call, result ->
                when (call.method) {
                    "startCapture" -> {
                        pendingHost = call.argument<String>("host") ?: ""
                        pendingPort = call.argument<Int>("port") ?: 7400
                        val manager = getSystemService(MediaProjectionManager::class.java)
                        @Suppress("DEPRECATION")
                        startActivityForResult(
                            manager.createScreenCaptureIntent(),
                            REQUEST_MEDIA_PROJECTION
                        )
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

    @Suppress("OVERRIDE_DEPRECATION", "DEPRECATION")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQUEST_MEDIA_PROJECTION) {
            if (resultCode == Activity.RESULT_OK && data != null) {
                val serviceIntent = Intent(this, AirShowCaptureService::class.java).apply {
                    putExtra("resultCode", resultCode)
                    putExtra("data", data)
                    putExtra("host", pendingHost)
                    putExtra("port", pendingPort)
                }
                ContextCompat.startForegroundService(this, serviceIntent)
            } else {
                eventSink?.success(
                    mapOf("type" to "ERROR", "message" to "Screen capture permission denied")
                )
            }
        }
    }
}
