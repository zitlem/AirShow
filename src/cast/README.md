# Cast receiver module

A Google Cast (CASTV2) receiver: TLS control channel on port 8009, device
authentication, the receiver/connection/heartbeat/media namespaces, and Cast
Streaming (mirroring) with AES-CTR decryption, frame reassembly and RTCP
feedback.

It doesn't depend on the rest of AirShow, only on Qt (Core, Network), OpenSSL
and protobuf. An application embeds it by implementing `CastReceiverHost`
(admission, approval, connection notifications, playback) and passing it to
`CastReceiver` with a set of `CastAuthCredentials`. AirShow's own host is
`src/protocol/CastHandler`.

## Licensing

Unlike the rest of AirShow (GPL-3.0), this directory is licensed under the
**Apache License 2.0** — see [LICENSE](LICENSE). The one exception:

- `cast_channel.proto` comes from Chromium and stays under its BSD-3-Clause
  license — see [LICENSE.chromium](LICENSE.chromium).

## Credentials are not included

Cast senders only accept receivers that present Google-signed device
credentials. This module contains no credentials and can't produce any; the
embedding application has to supply them (format documented in
`CastAuth.h`). The credentials AirShow ships in `resources/cast/` are
third-party data and are **not** covered by this license — see
`resources/cast/NOTICE`.
