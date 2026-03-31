# Phase 8: Miracast - Research

**Researched:** 2026-03-28
**Domain:** MS-MICE (Miracast over Infrastructure) — RTSP signaling + MPEG-TS/RTP media receive
**Confidence:** MEDIUM (protocol well-documented; receiver-side open-source implementations are sparse and mostly Python/GLib)

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

- **D-01:** Implement Miracast over Infrastructure (MS-MICE) ONLY. Do NOT implement Wi-Fi Direct Miracast. CLAUDE.md explicitly states: "v1: Skip Wi-Fi Direct Miracast entirely. Too fragile."
- **D-02:** MS-MICE works over the existing LAN (TCP for RTSP signaling, UDP for RTP media). No Wi-Fi P2P, no wpa_supplicant, no MiracleCast dependency.
- **D-03:** Primary target is Windows 10/11 which has built-in MS-MICE support via "Connect" / "Wireless Display" settings. Android support for infrastructure-mode Miracast is limited and varies by OEM — document as best-effort.
- **D-04:** Advertise the MS-MICE receiver via mDNS using `_display._tcp` service type. Add to DiscoveryManager alongside existing AirPlay/Cast/DLNA advertisements.
- **D-05:** Also advertise via DNS-SD TXT records with device capabilities: source-coupling, sink capabilities, supported codecs.
- **D-06:** Implement the MS-MICE RTSP signaling layer: SETUP (negotiate transport), PLAY (start streaming), PAUSE, TEARDOWN.
- **D-07:** Handle the WFD (Wi-Fi Display) capability negotiation in the RTSP OPTIONS/SETUP exchange. Return supported codecs (H.264 CBP/CHP) and audio formats (AAC-LC, LPCM).
- **D-08:** RTSP server listens on a dedicated port (default 7236, the standard WFD port for infrastructure mode).
- **D-09:** Use GStreamer pipeline: `rtpmp2tdepay → tsdemux → h264parse → hardware decode (vaapidecodebin on Linux, fallback avdec_h264)` for video, `aacparse → avdec_aac → audioconvert → autoaudiosink` for audio.
- **D-10:** New pipeline mode `initMiracastPipeline()` in MediaPipeline.
- **D-11:** Use existing `qml6glsink` and `autoaudiosink` output, consistent with all other protocols.
- **D-12:** Create `MiracastHandler : ProtocolHandler` in `src/protocol/MiracastHandler.h`.
- **D-13:** MiracastHandler owns the RTSP server socket on port 7236, manages WFD capability negotiation, and controls the GStreamer pipeline for each session.
- **D-14:** Single-session model consistent with all other protocols.
- **D-15:** SecurityManager integration: checkConnection() called before session establishment. Network filter (RFC1918) applies.
- **D-16:** ConnectionBridge HUD: shows "Miracast" as protocol and sender device name.

### Claude's Discretion

- RTSP server implementation details (Qt QTcpServer vs raw sockets)
- Exact WFD capability exchange format and values
- HDCP handling (skip for v1 — open-source receiver can't do HDCP)
- Whether to use GStreamer's `rtspsrc` element or manual RTSP parsing
- Exact MPEG-TS demux pipeline element chain
- Error handling for unsupported codec profiles
- UDP port negotiation for RTP media transport

### Deferred Ideas (OUT OF SCOPE)

- Wi-Fi Direct Miracast (deferred to v2 per CLAUDE.md)
- HDCP content protection (deferred — requires licensed HDCP keys)
</user_constraints>

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| MIRA-01 | User can mirror their Windows device screen to AirShow via Miracast | MS-MICE over LAN: TCP/7250 + RTSP/7236 + RTP/UDP. Confirmed feasible with Windows 10 version 1703+. |
| MIRA-02 | User can mirror their Android device screen to AirShow via Miracast (where supported) | Android MS-MICE support is OEM-specific and uncommon. Best-effort; same receiver code handles both if Android uses MS-MICE. Document limitation clearly. |
| MIRA-03 | Miracast mirroring includes synchronized audio and video | MPEG-TS muxes audio+video with shared PCR clock. GStreamer tsdemux preserves timing. AAC-LC and LPCM both viable. |
</phase_requirements>

---

## Summary

MS-MICE (Miracast over Infrastructure Connection Establishment Protocol, [MS-MICE] revision 6.0, April 2024) is a publicly documented Microsoft open protocol that extends Miracast to work over a standard LAN instead of Wi-Fi Direct. The protocol uses two TCP connections and one UDP RTP stream: the sink listens on TCP port 7250 for the Source Ready message from the Windows source, then connects back to the source's RTSP server on TCP port 7236 to negotiate the WFD (Wi-Fi Display) session, and finally receives an MPEG-TS multiplexed H.264+AAC stream over UDP RTP.

The critical architectural insight is that **the roles are reversed** from typical server-client expectations. The Windows source (PC being mirrored) is the RTSP server; the sink (AirShow) is the RTSP client that connects to it. AirShow initiates the RTSP connection to port 7236 after receiving the SOURCE_READY message on TCP 7250. The WFD RTSP negotiation follows a defined M1–M7 message sequence where the source offers and the sink acknowledges codec/resolution parameters.

The receiver-side GStreamer pipeline is a clean MPEG-TS demux: `udpsrc → rtpmp2tdepay → tsparse → tsdemux → [video: h264parse → hardware_decode → videoconvert → glupload → qml6glsink] [audio: aacparse → avdec_aac → audioconvert → autoaudiosink]`. This pattern is well-supported by GStreamer 1.26.x and is available on the dev machine (all required plugins confirmed present).

**Primary recommendation:** Implement MiracastHandler as a two-socket state machine — TCP listener on port 7250 (sink role: wait for SOURCE_READY) plus TCP connector to source port 7236 (RTSP client role: WFD M1-M7 negotiation) — then open a UDP socket at the negotiated port for RTP MPEG-TS media. The WFD RTSP exchange can be handled with a simple QTcpSocket state machine since the M-message sequence is deterministic and short (~7 messages). No external RTSP library needed.

---

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Qt6::Network (QTcpServer, QTcpSocket, QUdpSocket) | 6.8 LTS (already linked) | TCP signaling on ports 7250 and 7236, UDP RTP receive | Already in project; Qt event loop integration; async I/O without threads |
| GStreamer `rtpmp2tdepay` | 1.26.x (`gst-plugins-good`) | Strip RTP header from MPEG-TS payload | Standard element; confirmed in gst-plugins-good-1.26.5 installed |
| GStreamer `tsdemux` | 1.26.x (`gst-plugins-bad`) | Demultiplex MPEG-TS into video/audio elementary streams | Only mature TS demuxer in GStreamer |
| GStreamer `h264parse` | 1.26.x (`gst-plugins-bad`) | Parse H.264 bitstream, insert SPS/PPS | Required before hardware decoders |
| GStreamer `avdec_h264` | 1.26.x (`gst-libav`) | Software H.264 fallback decode | Same fallback used by AirPlay (Phase 4) |
| GStreamer `aacparse` + `avdec_aac` | 1.26.x (`gst-plugins-bad` + `gst-libav`) | AAC-LC audio parse and decode | Proven from existing appsrc audio pipeline |
| Avahi (AvahiAdvertiser) | already linked | mDNS `_display._tcp` advertisement | DiscoveryManager already uses it for AirPlay/Cast |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| GStreamer `tsparse` | 1.26.x | Align TS packets before tsdemux | Insert between rtpmp2tdepay and tsdemux; prevents alignment errors |
| GStreamer `vaapidecodebin` | 1.26.x | Linux hardware H.264/H.265 decode | Primary decoder when VAAPI available (same as AirPlay path) |
| GStreamer `queue` | built-in | Decouple pipeline branches | Required between tsdemux pads and decoders; tsdemux emits dynamic pads |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Manual RTSP parsing (QTcpSocket state machine) | GstRTSPServer (gstreamer-rtsp-server) | GstRTSPServer is a sender-oriented library with complex API. WFD requires custom GET_PARAMETER/SET_PARAMETER extensions not supported by stock GstRTSPServer. Manual parsing is ~150 lines and far simpler for a fixed 7-message exchange. |
| Manual RTSP parsing | Qt RTSP library | Qt does not ship an RTSP parser. QNetworkRequest/QNetworkAccessManager is HTTP-only. |
| Qt QTcpSocket for port 7250 | Raw POSIX sockets | Qt sockets integrate with event loop; no threading needed. Same pattern as CastHandler (QSslServer). |

**Installation:** All dependencies are already present — Qt6 Network is linked, GStreamer 1.26.x plugins are installed (`gst-plugins-good`, `gst-plugins-bad`, `gst-libav` confirmed on dev machine). No new packages required.

---

## Architecture Patterns

### Recommended Project Structure
```
src/protocol/
├── MiracastHandler.h        # ProtocolHandler subclass; owns TCP listeners + RTSP client
├── MiracastHandler.cpp      # MS-MICE + WFD-RTSP state machine implementation

src/pipeline/
├── MediaPipeline.h          # Add initMiracastPipeline() declaration
├── MediaPipeline.cpp        # Add MPEG-TS/RTP receive pipeline implementation

src/discovery/
├── DiscoveryManager.cpp     # Add _display._tcp advertisement in start()
```

### Pattern 1: Two-Phase Connection (MS-MICE Flow)

**What:** MS-MICE sink listens passively on TCP port 7250. When Windows source connects and sends SOURCE_READY (binary TLV message containing the source's RTSP port and friendly name), the sink connects TCP to the source's IP:7236 and begins the WFD RTSP client handshake.

**When to use:** This is the only MS-MICE flow for v1. It is always initiated by the Windows source scanning for sinks.

```
MS-MICE Projection Phase (verified from [MS-MICE] spec, revision 6.0):

  Windows Source                          AirShow (Sink)
       |                                        |
       |  TCP connect to sink:7250              |
       |--------------------------------------> |
       |                                        |  (no authentication in basic mode)
       |  SOURCE_READY msg (binary TLV)         |
       |  [Size:2][Version:0x01][Cmd:0x01]      |
       |  TLVs: FriendlyName, RTSPPort, SourceID|
       |--------------------------------------> |
       |                                        |
       |        TCP connect to source:7236      |
       |<-------------------------------------- |
       |                                        |
       |  [WFD RTSP M1-M7 exchange — see P2]   |
       |<=====================================> |
       |                                        |
       |  RTP/UDP MPEG-TS stream to sink port   |
       |--------------------------------------> |
```

### Pattern 2: WFD RTSP M-Message Exchange (Sink as RTSP Client)

**What:** The sink (AirShow) acts as RTSP client connecting to source port 7236. The exchange follows a deterministic 7-message sequence. The sink must respond with its decode capabilities in M3 GET_PARAMETER response.

**When to use:** Always — this is the WFD session setup protocol. No deviations needed for v1.

```
Message exchange (verified from lazycast d2.py, benzea/gnome-network-displays wfd-client.c,
and Wi-Fi Display Spec v2.1):

Seq  Direction       Method            Key payload
---  ---------       ------            -----------
M1   Source→Sink     OPTIONS           Require: org.wfa.wfd1.0
     Sink→Source     200 OK            Public: OPTIONS,SET_PARAMETER,GET_PARAMETER,SETUP,TEARDOWN,PLAY,PAUSE
M2   Sink→Source     OPTIONS           Require: org.wfa.wfd1.0
     Source→Sink     200 OK
M3   Source→Sink     GET_PARAMETER     Body: wfd_video_formats\r\nwfd_audio_codecs\r\n...
     Sink→Source     200 OK            Body: [capability response — see below]
M4   Source→Sink     SET_PARAMETER     Body: selected codec + RTP ports
     Sink→Source     200 OK
M5   Source→Sink     SET_PARAMETER     wfd_trigger_method: SETUP
     Sink→Source     200 OK
M6   Sink→Source     SETUP             rtsp://source-ip:7236/wfd1.0/streamid=0
                                       Transport: RTP/AVP/UDP;unicast;client_port=1028
     Source→Sink     200 OK            Transport: RTP/AVP/UDP;unicast;client_port=1028;server_port=XXXX
M7   Sink→Source     PLAY
     Source→Sink     200 OK            (stream begins)
```

**M3 Capability Response — sink advertises decoder support:**
```
wfd_video_formats: 00 00 02 10 0001FEFF 3FFFFFFF 00000FFF 00 0000 0000 00 none none
```
Field breakdown (Wi-Fi Display Technical Spec v2.1):
- `00` — native_resolution (no native preference)
- `00` — display_mode_supported (0 = no preferred mode)
- `02` — H264 codec count (2 descriptors: CBP + CHP)
- `10` — profile bitmask (bit 4 = CHP, bit 0 = CBP; `0x10` = CHP, `0x01` = CBP; use `0x11` for both)
- `0001FEFF` — level bitmask: CEA resolution support (bits 0-31 map to CEA modes; 1080p30 = bit 0x1C)
- `3FFFFFFF` — VESA resolution bitmask (all modes up to 1920x1200)
- `00000FFF` — HH (handheld) resolution bitmask
- `00` — latency
- `0000` — min_slice_size
- `0000` — slice_enc_params
- `00` — frame_rate_control_support
- `none none` — max_hres max_vres (none = accept whatever source sends)

**Audio codecs:**
```
wfd_audio_codecs: LPCM 00000003 00, AAC 00000001 00
```
- LPCM 00000003 = 44.1kHz + 48kHz stereo (bits 0,1 set)
- AAC 00000001 = 48kHz 2-channel (bit 0 = 48kHz stereo)

### Pattern 3: MiracastHandler State Machine

**What:** Single QObject managing both TCP sockets and one UdpSocket. Uses Qt signals/slots on the main event loop thread — same pattern as CastHandler which is fully event-loop based.

```cpp
// Rough state machine (implementation detail — for planner reference)
enum class State {
    Idle,
    WaitingSourceReady,   // listening on TCP 7250
    ConnectingToSource,   // connecting TCP to source:7236
    NegotiatingM1,        // sent M1 OPTIONS, waiting response
    NegotiatingM2,        // sending M2 OPTIONS
    NegotiatingM3,        // sent GET_PARAMETER, waiting
    NegotiatingM4,        // waiting for SET_PARAMETER from source
    NegotiatingM5,        // waiting for trigger SETUP
    SendingSetup,         // sent SETUP request, waiting response
    SendingPlay,          // sent PLAY, waiting response
    Streaming,
    TearingDown
};
```

### Pattern 4: initMiracastPipeline() — MPEG-TS/RTP Receive Pipeline

**What:** Add a new pipeline mode to MediaPipeline for MPEG-TS over RTP receive. Uses dynamic pads from tsdemux (same pattern as uridecodebin in initUriPipeline).

```cpp
// Source: verified from GStreamer pipeline samples + WFD spec
// Pipeline description (gst_parse_launch equivalent):
// udpsrc name=rtpsrc port=<negotiated> caps="application/x-rtp,media=video,
//   clock-rate=90000,encoding-name=MP2T" !
// rtpmp2tdepay ! tsparse ! tsdemux name=demux
// demux. ! queue ! h264parse ! vaapidecodebin ! videoconvert ! glupload ! qml6glsink
// demux. ! queue ! aacparse ! avdec_aac ! audioconvert ! audioresample ! autoaudiosink

// Key: tsdemux emits dynamic pads. Use pad-added signal like uridecodebin in Phase 5.
// tsdemux pad name pattern: "video_0_xxxx" and "audio_0_xxxx"
```

**Note on udpsrc vs appsrc:** For MPEG-TS/RTP the GStreamer pipeline uses `udpsrc` bound to the negotiated client port, not `appsrc`. This is different from AirPlay (which uses appsrc because UxPlay delivers decoded frames via callbacks). With MS-MICE, GStreamer receives RTP UDP packets directly from the source — the pipeline is fully pull-based via the network.

### Anti-Patterns to Avoid
- **Using GstRTSPServer for WFD:** Stock GstRTSPServer is a source/sender library. It cannot act as WFD sink — the handshake order (sink connects to source) is backwards from what GstRTSPServer provides.
- **Using rtspsrc element:** GStreamer's `rtspsrc` element handles standard RTSP GET streams but does NOT implement WFD-specific GET_PARAMETER/SET_PARAMETER capability negotiation. Do not use it; implement the RTSP exchange manually.
- **Using appsrc for MPEG-TS:** Unlike AirPlay, Miracast streams RTP to a UDP socket. Use `udpsrc` in the pipeline — not `appsrc`. The source pushes directly to the network; no callback injection needed.
- **Blocking M-message parsing:** The WFD RTSP exchange must be async (non-blocking). Use QTcpSocket `readyRead()` signal with an accumulation buffer. Same pattern as CastSession (ReadState enum for framing) — see STATE.md: "CastSession TCP framing uses accumulation buffer state machine."
- **HDCP:** Do NOT advertise HDCP support in wfd_content_protection. Without a licensed HDCP key, advertising it will cause Windows to refuse the connection when HDCP is required. Return `wfd_content_protection: none` in M3.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| MPEG-TS demultiplexing | Custom TS packet parser | GStreamer `tsdemux` | TS spec is complex: PAT/PMT tables, PID reassignment, PCR clock, elementary stream sync. Dozens of edge cases. |
| RTP packet handling | Custom RTP depayloader | GStreamer `rtpmp2tdepay` | RTP sequence reordering, jitter buffer, SSRC management |
| H.264 video decode | Custom H.264 decoder | GStreamer `h264parse + avdec_h264` / `vaapidecodebin` | Same decoders already used by AirPlay (Phase 4) — proven |
| AAC audio decode | Custom AAC decoder | GStreamer `aacparse + avdec_aac` | FFmpeg's AAC decoder is used as gst-libav avdec_aac — battle-tested |
| A/V sync | Manual PTS timestamping | GStreamer clock + tsdemux PCR | MPEG-TS carries PCR clock; tsdemux extracts it and GStreamer pipeline uses it for sync |
| Binary TLV parsing | Bespoke TLV decoder | Simple `uint16_t size; uint8_t version; uint8_t cmd;` struct | SOURCE_READY is 4-byte header + simple TLVs. Deserves 20 lines of C++, not a library. |

**Key insight:** The only genuinely custom code in this phase is the WFD RTSP text protocol parser (~150 lines) and the MS-MICE binary TLV parser (~50 lines). Everything else delegates to GStreamer.

---

## Common Pitfalls

### Pitfall 1: Discovery Is Required for Windows to Find the Receiver
**What goes wrong:** Windows "Connect" app will not show AirShow unless it can discover it via mDNS `_display._tcp`. Without this advertisement, the receiver is invisible — Windows falls back to Wi-Fi Direct scan and finds nothing.
**Why it happens:** MS-MICE uses mDNS for discovery, not a fixed IP scan. The Windows Connect app resolves the hostname from the mDNS Vendor Extension attribute.
**How to avoid:** Add `_display._tcp` advertisement in `DiscoveryManager::start()`. Use the existing AvahiAdvertiser pattern. Port is 7250. TXT record must include the sink hostname (machine hostname) so Windows can resolve it.
**Warning signs:** Windows "Connect" shows no devices in the wireless display list.

### Pitfall 2: SOURCE_READY is Big-Endian Binary, Not RTSP Text
**What goes wrong:** Developer treats port 7250 as an RTSP text socket and tries to parse "RTSP/1.0" messages from it. Gets garbage.
**Why it happens:** MS-MICE protocol on port 7250 is a custom binary TLV protocol, not RTSP. RTSP only begins after SOURCE_READY is received and the sink connects to port 7236.
**How to avoid:** Separate the two connections. `m_miceServer` (QTcpServer on 7250) handles binary TLV parsing. `m_rtspSocket` (QTcpSocket connecting to source:7236) handles RTSP text.
**Warning signs:** Crash or hang immediately after Windows tries to connect.

### Pitfall 3: Sink Must Send M2 OPTIONS Before M3 GET_PARAMETER
**What goes wrong:** Sink responds to M1 OPTIONS and immediately processes M3 GET_PARAMETER but skips sending M2 OPTIONS. Windows source either hangs or disconnects.
**Why it happens:** The WFD spec requires the sink to query the source's capabilities with its own OPTIONS request (M2) after responding to M1. This is not optional.
**How to avoid:** After sending 200 OK to M1, immediately send M2 OPTIONS with `Require: org.wfa.wfd1.0`. Process M3 GET_PARAMETER only after receiving the M2 200 OK response.
**Warning signs:** Session hangs after initial OPTIONS exchange; Wireshark shows M2 missing.

### Pitfall 4: tsdemux Pads Are Dynamic — Must Use pad-added Signal
**What goes wrong:** Pipeline construction hard-codes tsdemux output pads and linking fails at runtime with "could not link tsdemux to h264parse: no pad named video_0_0041".
**Why it happens:** tsdemux creates pads dynamically when it identifies the TS stream contents via PAT/PMT. The pad name includes the PID which varies.
**How to avoid:** Connect `g_signal_connect(tsdemux, "pad-added", G_CALLBACK(on_pad_added), ...)` and link h264parse and aacparse in the callback. Same pattern used for uridecodebin in Phase 5 (`initUriPipeline`).
**Warning signs:** Linking error at pipeline startup; video never appears.

### Pitfall 5: UDP Source Port Must Match M6 SETUP client_port
**What goes wrong:** MiracastHandler opens a UDP socket on an arbitrary port but the SETUP response from the source sends RTP to a different port number, causing media to be lost.
**Why it happens:** The client_port in the RTSP SETUP request specifies where the sink expects RTP. The source sends media to that exact port.
**How to avoid:** Bind the GStreamer `udpsrc` BEFORE sending the SETUP request. Extract the bound port with `getsockname()` or use a fixed port (1028 as used by lazycast). Encode this port in the SETUP Transport header: `Transport: RTP/AVP/UDP;unicast;client_port=1028`.
**Warning signs:** No video/audio despite PLAY succeeding; Wireshark shows RTP packets arriving on wrong port.

### Pitfall 6: wfd_content_protection Must Return "none"
**What goes wrong:** Developer returns a non-empty content_protection capability in M3. Windows interprets this as HDCP support and sends HDCP-encrypted media. Receiver cannot decrypt it and shows corrupted video.
**Why it happens:** Windows sources respect the sink's advertised content protection level. If the sink advertises anything other than "none", Windows may enable HDCP.
**How to avoid:** Always return `wfd_content_protection: none` in M3 capability response.
**Warning signs:** Video connects but shows green/black artifacts or no picture despite RTP packets arriving.

### Pitfall 7: SecurityManager Must Be Called Before Accepting MS-MICE Session
**What goes wrong:** MiracastHandler starts streaming immediately when SOURCE_READY arrives, bypassing the approval prompt.
**Why it happens:** MS-MICE has no built-in PIN/approval mechanism in the basic flow (Security Handshake is optional in v1).
**How to avoid:** After parsing SOURCE_READY and extracting the friendly name, call `m_securityManager->checkConnection(friendlyName, "miracast", sourceIp)` BEFORE connecting to port 7236. This is synchronous because MiracastHandler runs on a non-Qt thread (TCP server thread). See AirPlay pattern.
**Warning signs:** SecurityManager integration test fails; connections bypass approval dialog.

### Pitfall 8: Android MS-MICE Support Is Extremely Limited
**What goes wrong:** Developer tests Android devices and finds MS-MICE connection never works, concluding the implementation is broken.
**Why it happens:** Google Pixel and most Android devices do not support MS-MICE — they only use Wi-Fi Direct Miracast. MS-MICE on Android requires OEM implementation (uncommon as of 2024-2025).
**How to avoid:** Document MIRA-02 as best-effort. Do not tune the receiver for Android; if the same receiver works for a specific Android OEM that implements MS-MICE, great. No special Android code path needed.
**Warning signs:** Not a bug — expected behavior.

---

## Code Examples

Verified patterns from official sources and existing project code:

### SOURCE_READY Message Parsing (MS-MICE binary TLV)
```cpp
// Source: [MS-MICE] spec section 2.2.1 — big-endian format
// Header: [Size:2][Version:0x01][Command:0x01]
// TLVs follow in any order

struct MiceHeader {
    uint16_t size;    // big-endian: total message size in bytes
    uint8_t  version; // always 0x01
    uint8_t  command; // SOURCE_READY=0x01, STOP_PROJECTION=0x02
};

// TLV types for SOURCE_READY:
// 0x0001 = FriendlyName (UTF-16LE string, not null-terminated)
// 0x0002 = RTSPPort (2-byte big-endian port number)
// 0x0003 = SourceID (16-byte ASCII identifier)

void MiracastHandler::parseMiceMessage(const QByteArray& data) {
    if (data.size() < 4) return;
    uint16_t size    = qFromBigEndian<uint16_t>(data.data());
    uint8_t  version = data[2];
    uint8_t  command = data[3];
    // Parse TLVs starting at offset 4...
}
```

### _display._tcp mDNS Advertisement (DiscoveryManager integration)
```cpp
// Source: lazycast README, MS-MICE Surface Hub docs — port 7250, _display._tcp
static constexpr uint16_t kMiracastPort = 7250;

// In DiscoveryManager::start():
std::vector<TxtRecord> miracastTxt = {
    {"VerMgmt",  "0x0202"},     // MS-MICE version 2.2
    {"VerMin",   "0x0100"},     // WFD version 1.0
};
m_advertiser->advertise("_display._tcp", name, kMiracastPort, miracastTxt);
```

### M3 GET_PARAMETER Capability Response Body
```cpp
// Source: lazycast d2.py (verified against WFD spec v2.1 field definitions)
// Use this exact string as the M3 response body for maximum Windows compatibility:
static constexpr const char* kWfdCapabilityResponse =
    "wfd_video_formats: "
    "00 00 02 10 "                    // native=0, display_mode=0, H264 codecs: CHP+CBP
    "0001FEFF 3FFFFFFF 00000FFF "     // CEA + VESA + HH resolution bitmasks
    "00 0000 0000 00 none none\r\n"   // latency, slices, frame_rate_ctrl, hres/vres
    "wfd_audio_codecs: "
    "LPCM 00000003 00, AAC 00000001 00\r\n"
    "wfd_client_rtp_ports: "
    "RTP/AVP/UDP;unicast 1028 0 mode=play\r\n"
    "wfd_content_protection: none\r\n"
    "wfd_display_edid: none\r\n"
    "wfd_connector_type: 05\r\n";   // 05 = no physical connector (wireless)
```

### initMiracastPipeline() — MPEG-TS/RTP Receive Pipeline
```cpp
// Source: GStreamer pipeline samples + WFD/Miracast implementations (verified from
// GNOME network-displays wfd-media-factory.c sender pipeline — reversed for receive)
bool MediaPipeline::initMiracastPipeline(void* qmlVideoItem, int udpPort) {
    // Build as string for gst_parse_launch (easier than element-by-element for dynamic pads):
    //
    //  udpsrc name=rtpsrc port=<udpPort>
    //    caps="application/x-rtp,media=video,clock-rate=90000,encoding-name=MP2T"
    //  ! rtpmp2tdepay ! tsparse ! tsdemux name=demux
    //
    //  demux. ! queue ! h264parse ! [vaapidecodebin|avdec_h264]
    //         ! videoconvert ! glupload ! qml6glsink
    //
    //  demux. ! queue ! aacparse ! avdec_aac
    //         ! audioconvert ! audioresample ! autoaudiosink
    //
    // CRITICAL: tsdemux pads are dynamic — connect pad-added signal before
    // transitioning to PLAYING. Same pattern as initUriPipeline() Phase 5.
}
```

### RTSP M-Message Construction
```cpp
// Source: RFC 2326 (RTSP/1.0) + WFD spec — standard RTSP text format
// Each message has a CSeq header that must increment per request/response.

QString MiracastHandler::buildRtspResponse(int cseq, int statusCode,
                                            const QString& body) {
    QString resp = QString("RTSP/1.0 %1 OK\r\n"
                           "CSeq: %2\r\n"
                           "Server: AirShow/1.0\r\n")
                   .arg(statusCode).arg(cseq);
    if (!body.isEmpty()) {
        resp += QString("Content-Type: text/parameters\r\n"
                        "Content-Length: %1\r\n").arg(body.toUtf8().size());
    }
    resp += "\r\n";
    if (!body.isEmpty()) resp += body;
    return resp;
}
```

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Wi-Fi Direct P2P Miracast (kernel wpa_supplicant P2P) | MS-MICE over infrastructure LAN | Windows 10 version 1703 (2017) | Enterprise-grade LAN use is now the reliable path; Wi-Fi Direct is fragile |
| MiracleCast (P2P) for Linux Miracast | GNOME Network Displays (MICE + Chromecast) | Jan 2024 (0.91 release) | First serious Linux MICE sender implementation; no receiver equivalent |
| Android Miracast via Wi-Fi Direct | Android Miracast via MS-MICE (OEM-specific) | N/A — Android still primarily uses Wi-Fi Direct | MIRA-02 remains best-effort for Android |

**Deprecated/outdated:**
- MiracleCast (albfan/miraclecast): Development stalled per maintainer. wpa_supplicant P2P API unstable. Do not use.
- Intel WDS (intel/wds): Last commit 2016. Targeting sender side (Linux as Miracast source). Not relevant.
- RPiPlay-style OMX pipelines: Raspberry Pi specific. Irrelevant.

---

## Open Questions

1. **Exact _display._tcp TXT record fields Windows expects**
   - What we know: Port 7250 required; hostname must be DNS-resolvable; mDNS `_display._tcp` required
   - What's unclear: Which specific TXT key-value pairs Windows "Connect" app validates. GNOME Network Displays uses minimal TXT records. Airtame uses `VerMgmt`/`VerMin` fields.
   - Recommendation: Start with empty TXT or minimal `{VerMgmt: 0x0202, VerMin: 0x0100}`. Windows Connect primarily resolves by hostname, not TXT content. Test and iterate.

2. **Whether MiracastHandler should be QObject (event-loop based) or thread-based**
   - What we know: CastHandler is fully QObject-based on Qt event loop. AirPlayHandler uses background threads (UxPlay's own). SecurityManager checkConnection() is synchronous (blocks) for non-Qt threads, async for Qt thread.
   - What's unclear: Port 7250 TCP accept + WFD RTSP M-message exchange — can this be done entirely on the Qt event loop without blocking?
   - Recommendation: Use QObject + QTcpServer on Qt event loop (same as CastHandler). The M1-M7 exchange is async text protocol — fully compatible with Qt's readyRead() pattern. Call `checkConnection()` synchronously in a brief QEventLoop spin or via async pattern (D-15 says "checkConnection" but CastHandler uses async — pick async for consistency).

3. **UDP port selection for RTP receive**
   - What we know: lazycast uses fixed port 1028. Standard WFD implementations use 1028 or negotiate.
   - What's unclear: Whether port 1028 requires root/special privileges on some systems (ports < 1024 do; 1028 is fine).
   - Recommendation: Use fixed port 1028 (matches lazycast, avoids port negotiation complexity for v1).

4. **tsdemux pad naming for video vs audio**
   - What we know: tsdemux creates pads dynamically named `video_0_<PID_hex>` and `audio_0_<PID_hex>`. The PID values are defined in the TS PAT/PMT tables sent by the source.
   - What's unclear: Whether Windows sources reliably use the WFD-standard PIDs (video PID 0x1011 = 4113, audio PID 0x1100 = 4352 — from GNOME media factory).
   - Recommendation: Detect pad type from the GstCaps media field in the pad-added callback (e.g., `video/x-h264` vs `audio/mpeg`) rather than matching pad names. This is robust against PID variation.

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| GStreamer gst-plugins-good | `rtpmp2tdepay` | Yes | 1.26.5 | — |
| GStreamer gst-plugins-bad | `tsdemux`, `h264parse`, `vaapidecodebin` | Yes | 1.26.5 | `avdec_h264` for decode |
| GStreamer gst-libav | `avdec_h264`, `avdec_aac` | Yes | 1.26.6 | — |
| Qt6 Network | QTcpServer, QTcpSocket, QUdpSocket | Yes | 6.8 (project already links) | — |
| Avahi | `_display._tcp` mDNS advertisement | Yes | vendored in project | — |
| GStreamer `tsparse` | Alignment before tsdemux | Confirmed (gst-plugins-bad) | 1.26.5 | Omit — tsdemux works without but alignment is safer |

**Missing dependencies with no fallback:** None.

**Missing dependencies with fallback:** None. All required GStreamer elements are present.

---

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Google Test (via existing test infrastructure) |
| Config file | tests/CMakeLists.txt |
| Quick run command | `cmake --build build --target test_miracast && ./build/test_miracast` |
| Full suite command | `cmake --build build && ctest --output-on-failure` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| MIRA-01 | Windows device discovers and connects via MS-MICE | integration / smoke | `./build/test_miracast --gtest_filter=MiracastHandler.*` | No — Wave 0 |
| MIRA-02 | Android Miracast (best-effort) | manual-only | Manual: connect Android device that supports MS-MICE | N/A |
| MIRA-03 | Audio+video sync | unit (pipeline) | `./build/test_miracast --gtest_filter=MediaPipeline.MiracastPipelineInit` | No — Wave 0 |

**MIRA-02 is manual-only** because Android MS-MICE support is OEM-specific and cannot be automated in CI.

### Sampling Rate
- **Per task commit:** `cmake --build build --target test_miracast && ./build/test_miracast`
- **Per wave merge:** `cmake --build build && ctest --output-on-failure`
- **Phase gate:** Full suite green before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `tests/test_miracast.cpp` — covers MIRA-01 (MiracastHandler construction, start/stop, MS-MICE binary parsing), MIRA-03 (MediaPipeline::initMiracastPipeline existence)
- [ ] `tests/CMakeLists.txt` update — add `test_miracast` target linking `MiracastHandler.cpp`, `MediaPipeline.cpp`, Qt6::Network, GStreamer

---

## Sources

### Primary (HIGH confidence)
- [MS-MICE Protocol Spec revision 6.0, April 2024](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-mice/9598ca72-d937-466c-95f6-70401bb10bdb) — Protocol overview, message types confirmed
- [MS-MICE Protocol Details](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-mice/940d808c-97f8-418e-a8a9-c471dc0d21bb) — Full 3-phase session flow, port 7250 and 7236 roles confirmed
- [MS-MICE Message Syntax](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-mice/54c342ee-e2d1-4db5-95e2-29b91611be36) — Binary TLV format, big-endian, all 6 message types
- [MS-MICE Source Ready Message](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-mice/8f34dbd9-167f-4cd9-b348-b20f710240f9) — SOURCE_READY structure: [Size:2][Version:1][Cmd:1][TLVs]
- [Microsoft Surface Hub Miracast over Infrastructure Guide](https://learn.microsoft.com/en-us/surface-hub/miracast-over-infrastructure) — Port requirements (7250, 7236), mDNS requirement, Windows 10 1703+ confirmed
- [GNOME Network Displays source (benzea/gnome-network-displays wfd-client.c)](https://github.com/benzea/gnome-network-displays/blob/master/src/wfd/wfd-client.c) — M1-M7 WFD RTSP state machine, capability string format
- [GNOME Network Displays source (GNOME/gnome-network-displays nd-wfd-mice-sink.c)](https://github.com/GNOME/gnome-network-displays) — SOURCE_READY parsing, RTSP server setup after MICE
- [GNOME Network Displays wfd-media-factory.c sender pipeline](https://github.com/benzea/gnome-network-displays/blob/master/src/wfd/wfd-media-factory.c) — MPEG-TS mux pipeline (reversed = receiver pipeline)

### Secondary (MEDIUM confidence)
- [lazycast d2.py](https://github.com/homeworkc/lazycast/blob/master/d2.py) — WFD RTSP M1-M7 exchange, wfd_video_formats capability string `00 00 02 10 0001FEFF 3FFFFFFF 00000FFF 00 0000 0000 00 none none`, audio codecs, UDP port 1028
- [GNOME Network Displays 0.91 release (Phoronix, Jan 2024)](https://www.phoronix.com/news/GNOME-Network-Displays-0.91) — MICE receiver feasibility confirmed (tested against LG WebOS TV over LAN)
- [Airtame Miracast over Infrastructure guide](https://help.airtame.com/hc/en-us/articles/5142664511517-How-to-present-with-Miracast-over-infrastructure) — Commercial product confirming port requirements
- GStreamer MPEG-TS over RTP pipeline samples — `rtpmp2tdepay ! tsparse ! tsdemux` chain pattern verified

### Tertiary (LOW confidence)
- Android MS-MICE support claims: Multiple forum reports confirm most Android devices (including Pixel) do NOT support MS-MICE. OEM-specific support (some Samsung, Cisco) documented in product literature but not independently verified.
- wfd_video_formats bit field values: Derived from lazycast empirical implementation + partial MS-WFDPE spec. The exact bitmask meaning for CEA/VESA resolution tables not fully verified against primary Wi-Fi Alliance spec (paywall).

---

## Project Constraints (from CLAUDE.md)

All directives below are active for this phase:

| Directive | Impact on Phase 8 |
|-----------|------------------|
| C++17, no exceptions | MiracastHandler must use C++17; parse TLVs with structured bindings/algorithms |
| Qt 6.8 LTS | Use QTcpServer, QTcpSocket, QUdpSocket — all in Qt6::Network |
| GStreamer 1.26.x | Use `rtpmp2tdepay`, `tsdemux`, `tsparse`, `h264parse`, `avdec_h264`, `aacparse`, `avdec_aac` |
| OpenSSL 3.x | Not directly needed for basic MS-MICE (no TLS on ports 7250/7236 in basic flow); skip DTLS for v1 |
| CMake + vcpkg | No new packages needed — all GStreamer plugins are system packages |
| ProtocolHandler interface | MiracastHandler must implement start/stop/name/isRunning/setMediaPipeline |
| Single-session model | Only one Miracast session at a time; new connection replaces old |
| No Wi-Fi Direct Miracast | Explicitly prohibited — MS-MICE only |
| Local network only (RFC1918) | SecurityManager::isLocalNetwork() filter applies; source IP must be private |
| clang-format | Apply to all new .h/.cpp files |

---

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — all GStreamer elements confirmed present on dev machine (dpkg output); Qt6::Network already linked
- Architecture: HIGH — MS-MICE protocol fully documented by Microsoft; WFD RTSP M-message sequence verified against two independent open-source implementations (GNOME Network Displays + lazycast)
- WFD capability strings: MEDIUM — specific bitmask values from lazycast empirical implementation; Wi-Fi Alliance spec is paywalled but lazycast is tested against real Windows 10/11 sources
- Android support: LOW — OEM-dependent; cannot be guaranteed
- HDCP skip: HIGH — confirmed correct for open-source receivers; advertising HDCP support without keys breaks connections

**Research date:** 2026-03-28
**Valid until:** 2026-09-28 (MS-MICE spec stable; GStreamer 1.26.x LTS)
