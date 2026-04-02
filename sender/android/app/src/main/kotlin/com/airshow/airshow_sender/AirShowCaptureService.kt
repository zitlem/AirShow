package com.airshow.airshow_sender

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.IBinder
import android.util.DisplayMetrics
import android.view.WindowManager
import androidx.annotation.RequiresApi
import androidx.core.app.NotificationCompat
import org.json.JSONObject
import java.io.OutputStream
import java.net.Socket
import kotlin.concurrent.thread

/**
 * AirShowCaptureService
 *
 * Foreground service that owns the entire capture-encode-stream pipeline:
 *   1. MediaProjection for screen capture consent
 *   2. VirtualDisplay feeding H264Encoder's input Surface
 *   3. H264Encoder (MediaCodec H.264) writing NAL units as 16-byte framed packets
 *   4. AudioPlaybackCapture (API 29+) for system audio, sent as TYPE_AUDIO frames
 *   5. TCP socket to the AirShow receiver on port 7400
 *   6. JSON HELLO/HELLO_ACK handshake before streaming
 *   7. EventChannel status events back to Dart via MainActivity.eventSink
 *
 * Started by MainActivity with resultCode + data extras from MediaProjection consent.
 * Stopped by intent with action="STOP" or by MainActivity's stopCapture call.
 */
class AirShowCaptureService : Service() {

    companion object {
        private const val NOTIFICATION_ID = 1001
        private const val CHANNEL_ID = "airshow_capture"

        // Maximum streaming resolution (landscape); aspect ratio is preserved
        private const val MAX_WIDTH = 1280
        private const val MAX_HEIGHT = 720
    }

    private var mediaProjection: MediaProjection? = null
    private var virtualDisplay: VirtualDisplay? = null
    private var encoder: H264Encoder? = null
    private var socket: Socket? = null
    private var outputStream: OutputStream? = null
    private var audioRecord: AudioRecord? = null

    @Volatile
    private var isRunning = false
    private var audioThread: Thread? = null
    private var streamThread: Thread? = null

    // -------------------------------------------------------------------
    // Service lifecycle
    // -------------------------------------------------------------------

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == "STOP") {
            stopSelf()
            return START_NOT_STICKY
        }

        val resultCode = intent?.getIntExtra("resultCode", -1) ?: -1
        val data: Intent? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent?.getParcelableExtra("data", Intent::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent?.getParcelableExtra("data")
        }
        val host = intent?.getStringExtra("host") ?: ""
        val port = intent?.getIntExtra("port", 7400) ?: 7400

        if (resultCode == -1 || data == null || host.isEmpty()) {
            postEvent(mapOf("type" to "ERROR", "message" to "Invalid capture parameters"))
            stopSelf()
            return START_NOT_STICKY
        }

        startForegroundNotification(host)

        streamThread = thread(name = "AirShowStreamThread") {
            runPipeline(resultCode, data, host, port)
        }

        return START_NOT_STICKY
    }

    override fun onDestroy() {
        super.onDestroy()
        teardown()
        postEvent(mapOf("type" to "DISCONNECTED", "reason" to "Service stopped"))
    }

    // -------------------------------------------------------------------
    // Pipeline
    // -------------------------------------------------------------------

    private fun runPipeline(resultCode: Int, data: Intent, host: String, port: Int) {
        try {
            // 1. Get MediaProjection
            val projManager = getSystemService(MediaProjectionManager::class.java)
            mediaProjection = projManager.getMediaProjection(resultCode, data)

            // 2. Get screen dimensions, clamped to max resolution
            val (width, height) = getStreamingResolution()

            // 3. Configure encoder — get input surface before creating virtual display
            encoder = H264Encoder(width, height, bitrate = 4_000_000, fps = 30)
            val inputSurface = encoder!!.configure()

            // 4. Connect TCP socket to receiver
            val sock = Socket(host, port).also {
                it.soTimeout = 5_000
                socket = it
            }
            val stream = sock.outputStream.also { outputStream = it }

            // 5. Send HELLO handshake
            val hello = JSONObject().apply {
                put("type", "HELLO")
                put("version", 1)
                put("deviceName", Build.MODEL)
                put("codec", "h264")
                put("maxResolution", "${width}x${height}")
                put("targetBitrate", 4_000_000)
                put("fps", 30)
            }
            stream.write("$hello\n".toByteArray(Charsets.UTF_8))
            stream.flush()

            // 6. Read HELLO_ACK — temporarily remove read timeout for handshake
            sock.soTimeout = 10_000
            val ackLine = sock.inputStream.bufferedReader().readLine()
                ?: throw IllegalStateException("Receiver closed connection before HELLO_ACK")
            val ack = JSONObject(ackLine)
            if (ack.optString("type") != "HELLO_ACK") {
                throw IllegalStateException("Expected HELLO_ACK, got: ${ack.optString("type")}")
            }
            sock.soTimeout = 0  // streaming: no timeout

            // 7. Notify Dart we are connected
            postEvent(mapOf("type" to "CONNECTED"))

            isRunning = true

            // 8. Create VirtualDisplay feeding the encoder surface
            virtualDisplay = mediaProjection!!.createVirtualDisplay(
                "AirShowCapture",
                width, height,
                getDisplayDensityDpi(),
                DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
                inputSurface,
                null, null
            )

            // 9. Start encoder — encoded NALs go directly to the socket
            encoder!!.start(stream) { errorMessage ->
                if (isRunning) {
                    postEvent(mapOf("type" to "ERROR", "message" to errorMessage))
                    stopSelf()
                }
            }

            // 10. Start audio capture if supported
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                startAudioCapture(stream)
            }

        } catch (e: Exception) {
            postEvent(mapOf("type" to "ERROR", "message" to "Pipeline error: ${e.message}"))
            stopSelf()
        }
    }

    // -------------------------------------------------------------------
    // Audio capture (API 29+)
    // -------------------------------------------------------------------

    @RequiresApi(Build.VERSION_CODES.Q)
    private fun startAudioCapture(stream: OutputStream) {
        try {
            val projection = mediaProjection ?: return
            val config = AudioPlaybackCaptureConfiguration.Builder(projection)
                .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
                .addMatchingUsage(AudioAttributes.USAGE_GAME)
                .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
                .build()

            val minBufSize = AudioRecord.getMinBufferSize(
                44100,
                AudioFormat.CHANNEL_IN_STEREO,
                AudioFormat.ENCODING_PCM_16BIT
            )
            val bufSize = maxOf(minBufSize, 4096)

            val record = AudioRecord.Builder()
                .setAudioPlaybackCaptureConfig(config)
                .setAudioFormat(
                    AudioFormat.Builder()
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setSampleRate(44100)
                        .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
                        .build()
                )
                .setBufferSizeInBytes(bufSize * 4)
                .build()
            audioRecord = record

            record.startRecording()

            audioThread = thread(name = "AirShowAudioThread") {
                val buffer = ByteArray(bufSize)
                while (isRunning) {
                    val read = record.read(buffer, 0, buffer.size)
                    if (read > 0) {
                        try {
                            val header = buildFrameHeader(TYPE_AUDIO, 0, read, System.nanoTime())
                            synchronized(stream) {
                                stream.write(header)
                                stream.write(buffer, 0, read)
                                stream.flush()
                            }
                        } catch (_: Exception) {
                            // Socket closed — streaming ended; exit audio loop
                            break
                        }
                    }
                }
                try {
                    record.stop()
                    record.release()
                } catch (_: Exception) {
                }
            }
        } catch (e: Exception) {
            // Audio capture is best-effort; log but don't fail the whole pipeline
            postEvent(mapOf("type" to "ERROR", "message" to "Audio capture failed: ${e.message}"))
        }
    }

    // -------------------------------------------------------------------
    // Teardown
    // -------------------------------------------------------------------

    private fun teardown() {
        isRunning = false

        audioThread?.interrupt()
        audioThread = null

        try {
            audioRecord?.stop()
            audioRecord?.release()
        } catch (_: Exception) {
        }
        audioRecord = null

        encoder?.stop()
        encoder = null

        virtualDisplay?.release()
        virtualDisplay = null

        mediaProjection?.stop()
        mediaProjection = null

        try {
            socket?.close()
        } catch (_: Exception) {
        }
        socket = null
        outputStream = null

        streamThread = null
    }

    // -------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------

    /** Compute stream resolution clamped to MAX_WIDTH x MAX_HEIGHT (aspect-ratio preserved). */
    private fun getStreamingResolution(): Pair<Int, Int> {
        val (rawW, rawH) = getScreenDimensions()

        // Always stream in landscape orientation
        val landscape = if (rawW >= rawH) Pair(rawW, rawH) else Pair(rawH, rawW)
        val (w, h) = landscape

        return if (w <= MAX_WIDTH && h <= MAX_HEIGHT) {
            Pair(w, h)
        } else {
            val scaleW = MAX_WIDTH.toFloat() / w
            val scaleH = MAX_HEIGHT.toFloat() / h
            val scale = minOf(scaleW, scaleH)
            // Align to 16 pixels (required by most H.264 encoders)
            val newW = (w * scale).toInt() and 0xFFFFFFF0.toInt()
            val newH = (h * scale).toInt() and 0xFFFFFFF0.toInt()
            Pair(maxOf(newW, 16), maxOf(newH, 16))
        }
    }

    private fun getScreenDimensions(): Pair<Int, Int> {
        val wm = getSystemService(WindowManager::class.java)
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val bounds = wm.currentWindowMetrics.bounds
            Pair(bounds.width(), bounds.height())
        } else {
            @Suppress("DEPRECATION")
            val metrics = DisplayMetrics()
            @Suppress("DEPRECATION")
            wm.defaultDisplay.getRealMetrics(metrics)
            Pair(metrics.widthPixels, metrics.heightPixels)
        }
    }

    private fun getDisplayDensityDpi(): Int {
        val wm = getSystemService(WindowManager::class.java)
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            wm.currentWindowMetrics.windowInsets
                .let { resources.displayMetrics.densityDpi }
        } else {
            @Suppress("DEPRECATION")
            val metrics = DisplayMetrics()
            @Suppress("DEPRECATION")
            wm.defaultDisplay.getRealMetrics(metrics)
            metrics.densityDpi
        }
    }

    /** Post an event map to the Dart EventChannel sink on the main thread. */
    private fun postEvent(event: Map<String, Any?>) {
        val sink = MainActivity.eventSink ?: return
        // EventSink.success must be called on the main thread
        android.os.Handler(mainLooper).post {
            try {
                sink.success(event)
            } catch (_: Exception) {
                // Sink may have been cancelled; ignore
            }
        }
    }

    // -------------------------------------------------------------------
    // Foreground notification
    // -------------------------------------------------------------------

    private fun startForegroundNotification(host: String) {
        val nm = getSystemService(NotificationManager::class.java)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "AirShow Mirroring",
                NotificationManager.IMPORTANCE_LOW
            )
            nm.createNotificationChannel(channel)
        }

        val stopIntent = PendingIntent.getService(
            this, 0,
            Intent(this, AirShowCaptureService::class.java).apply { action = "STOP" },
            PendingIntent.FLAG_IMMUTABLE
        )

        val notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("AirShow Mirroring")
            .setContentText("Mirroring to $host")
            .setSmallIcon(android.R.drawable.ic_media_play)
            .addAction(android.R.drawable.ic_media_pause, "Stop", stopIntent)
            .setOngoing(true)
            .build()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                NOTIFICATION_ID, notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }
}
