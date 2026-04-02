package com.airshow.airshow_sender

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.media.MediaFormat
import android.os.Handler
import android.os.HandlerThread
import android.view.Surface
import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

// Frame header constants matching AirShowHandler.h protocol definition.
// Header layout (16 bytes, big-endian):
//   Byte 0:    type    (0x01=VIDEO_NAL, 0x02=AUDIO, 0x03=KEEPALIVE)
//   Byte 1:    flags   (0x01=keyframe, 0x02=end_of_AU)
//   Bytes 2-5: length  (uint32 payload byte count, NOT including header)
//   Bytes 6-13: pts    (int64 nanoseconds)
//   Bytes 14-15: reserved (zero)
const val TYPE_VIDEO_NAL: Byte = 0x01
const val TYPE_AUDIO: Byte = 0x02
const val TYPE_KEEPALIVE: Byte = 0x03
const val FLAG_KEYFRAME: Byte = 0x01
const val FLAG_END_OF_AU: Byte = 0x02
const val HEADER_SIZE = 16

/**
 * Builds a 16-byte AirShow frame header in big-endian byte order.
 *
 * @param type     Frame type (TYPE_VIDEO_NAL, TYPE_AUDIO, TYPE_KEEPALIVE)
 * @param flags    Frame flags (FLAG_KEYFRAME, FLAG_END_OF_AU, or 0)
 * @param length   Payload byte count (NOT including this header)
 * @param ptsNs    Presentation timestamp in nanoseconds
 */
fun buildFrameHeader(type: Byte, flags: Byte, length: Int, ptsNs: Long): ByteArray {
    val buf = ByteBuffer.allocate(HEADER_SIZE).order(ByteOrder.BIG_ENDIAN)
    buf.put(type)
    buf.put(flags)
    buf.putInt(length)
    buf.putLong(ptsNs)
    buf.putShort(0)  // reserved
    return buf.array()
}

/**
 * H.264 hardware encoder backed by Android MediaCodec.
 *
 * Usage:
 *   val surface = encoder.configure()    // configure and get input surface
 *   encoder.start(outputStream) { err -> } // start encoding; output written to stream
 *   encoder.stop()                       // stop and release
 *
 * SPS/PPS config frames are cached and automatically prepended to every IDR (keyframe).
 * Output frames are written as 16-byte AirShow frame headers followed by NAL data.
 */
class H264Encoder(
    private val width: Int,
    private val height: Int,
    private val bitrate: Int = 4_000_000,
    private val fps: Int = 30
) {
    private var encoder: MediaCodec? = null
    private var handlerThread: HandlerThread? = null

    // Cached SPS+PPS config — prepended to every IDR frame so the receiver can
    // decode any keyframe without needing an out-of-band parameter set exchange.
    private var spsNalUnit: ByteArray? = null

    /**
     * Configure the encoder and return the input Surface.
     * The Surface must be set as the VirtualDisplay target before start() is called.
     */
    fun configure(): Surface {
        val format = MediaFormat.createVideoFormat("video/avc", width, height).apply {
            setInteger(
                MediaFormat.KEY_COLOR_FORMAT,
                MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface
            )
            setInteger(MediaFormat.KEY_BIT_RATE, bitrate)
            setInteger(MediaFormat.KEY_FRAME_RATE, fps)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 2)
            setInteger(
                MediaFormat.KEY_PROFILE,
                MediaCodecInfo.CodecProfileLevel.AVCProfileBaseline
            )
            setInteger(
                MediaFormat.KEY_LEVEL,
                MediaCodecInfo.CodecProfileLevel.AVCLevel31
            )
        }

        // Prefer hardware encoder; fall back to any available AVC encoder
        val codecName = MediaCodecList(MediaCodecList.REGULAR_CODECS).findEncoderForFormat(format)
        encoder = if (codecName != null) {
            MediaCodec.createByCodecName(codecName)
        } else {
            MediaCodec.createEncoderByType("video/avc")
        }

        encoder!!.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        return encoder!!.createInputSurface()
    }

    /**
     * Start encoding. Encoded NAL units are written to [outputStream] as AirShow frames.
     * [onError] is called on the encoder thread if a write or codec error occurs.
     */
    fun start(outputStream: OutputStream, onError: (String) -> Unit) {
        handlerThread = HandlerThread("H264EncoderThread").also { it.start() }
        val handler = Handler(handlerThread!!.looper)

        encoder!!.setCallback(object : MediaCodec.Callback() {
            override fun onInputBufferAvailable(codec: MediaCodec, index: Int) {
                // Surface mode: input buffers are not used directly; frames come from VirtualDisplay
            }

            override fun onOutputBufferAvailable(
                codec: MediaCodec,
                index: Int,
                info: MediaCodec.BufferInfo
            ) {
                try {
                    if (info.size <= 0) {
                        codec.releaseOutputBuffer(index, false)
                        return
                    }

                    val buffer = codec.getOutputBuffer(index) ?: run {
                        codec.releaseOutputBuffer(index, false)
                        return
                    }
                    val nalData = ByteArray(info.size)
                    buffer.get(nalData)
                    codec.releaseOutputBuffer(index, false)

                    if (info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG != 0) {
                        // Cache SPS+PPS for prepending to IDR frames
                        spsNalUnit = nalData.copyOf()
                        return
                    }

                    val isKeyframe = info.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME != 0
                    val ptsNs = info.presentationTimeUs * 1000L  // microseconds -> nanoseconds

                    if (isKeyframe && spsNalUnit != null) {
                        // Prepend cached SPS+PPS before the IDR so receiver can decode standalone
                        val combined = spsNalUnit!! + nalData
                        val header = buildFrameHeader(TYPE_VIDEO_NAL, FLAG_KEYFRAME, combined.size, ptsNs)
                        synchronized(outputStream) {
                            outputStream.write(header)
                            outputStream.write(combined)
                            outputStream.flush()
                        }
                    } else {
                        val flags: Byte = if (isKeyframe) FLAG_KEYFRAME else 0
                        val header = buildFrameHeader(TYPE_VIDEO_NAL, flags, nalData.size, ptsNs)
                        synchronized(outputStream) {
                            outputStream.write(header)
                            outputStream.write(nalData)
                            outputStream.flush()
                        }
                    }
                } catch (e: Exception) {
                    onError("Encoder write error: ${e.message}")
                }
            }

            override fun onError(codec: MediaCodec, e: MediaCodec.CodecException) {
                onError("MediaCodec error: ${e.message}")
            }

            override fun onOutputFormatChanged(codec: MediaCodec, format: MediaFormat) {
                // Format change is normal during startup — SPS/PPS may arrive here
            }
        }, handler)

        encoder!!.start()
    }

    /** Stop encoding and release all MediaCodec resources. */
    fun stop() {
        try {
            encoder?.stop()
            encoder?.release()
        } catch (_: Exception) {
        }
        handlerThread?.quitSafely()
        encoder = null
        handlerThread = null
        spsNalUnit = null
    }
}
