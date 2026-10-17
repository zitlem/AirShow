#!/usr/bin/env bash
# Assemble a self-contained Windows folder from an MSYS2 (UCRT64/MINGW64) build:
# airshow.exe + Qt (windeployqt) + the GStreamer plugins AirShow uses + every
# DLL they depend on. Run from an MSYS2 shell at the repo root:
#   packaging/windows/package.sh [build-dir] [output-dir]
set -euo pipefail

BUILD_DIR=${1:-build}
OUT=${2:-dist/AirShow}
PREFIX=${MINGW_PREFIX:?run this from an MSYS2 UCRT64/MINGW64 shell}

rm -rf "$OUT"
mkdir -p "$OUT/lib/gstreamer-1.0"

cp "$BUILD_DIR/airshow.exe" "$OUT/"
cp -r "$BUILD_DIR/resources" "$OUT/"

# Qt libraries, platform/TLS plugins and QML runtime.
WINDEPLOYQT=$(command -v windeployqt6 || command -v windeployqt-qt6 || command -v windeployqt)
"$WINDEPLOYQT" --release --qmldir qml --no-translations "$OUT/airshow.exe"

# MSYS2's Qt resolves QML imports relative to its install prefix
# (../share/qt6/qml); point it at windeployqt's layout next to the exe instead.
cat > "$OUT/qt.conf" <<'EOF'
[Paths]
Prefix = .
Plugins = .
QmlImports = qml
EOF

# GStreamer plugins used by the AirPlay, Cast, DLNA and Miracast pipelines
# (decodebin picks decoders from whatever is present). Missing ones are skipped
# with a note so a GStreamer package split doesn't break the build.
PLUGINS=(
  coreelements app playback typefindfunctions autodetect
  videoconvertscale videoconvert videoscale videotestsrc videorate
  audiotestsrc audioconvert audioresample volume
  wasapi wasapi2 directsound
  libav openh264 vpx opus dav1d d3d11 d3d12 mediafoundation
  videoparsersbad audioparsers dvdlpcmdec
  mpegtsdemux rtp rtpmanager udp tcp
  isomp4 matroska ogg vorbis flac wavparse id3demux icydemux mpg123 soup curl
)
for p in "${PLUGINS[@]}"; do
  f="$PREFIX/lib/gstreamer-1.0/libgst$p.dll"
  if [[ -f "$f" ]]; then
    cp "$f" "$OUT/lib/gstreamer-1.0/"
  else
    echo "note: GStreamer plugin '$p' not installed — skipped"
  fi
done

# The soup plugin (HTTP for DLNA) dlopen()s libsoup instead of linking it, so
# ldd can't see it; ship it explicitly (Windows searches the exe's directory).
for soup in "$PREFIX"/bin/libsoup-3.0-*.dll; do
  if [[ -f "$soup" ]]; then cp "$soup" "$OUT/"; fi
done

# Copy every MSYS2 DLL that the exe, Qt plugins or GStreamer plugins load,
# repeating until nothing new is found (dependencies of dependencies).
prefix_bin=$(cygpath -u "$PREFIX/bin")
while true; do
  added=0
  while IFS= read -r dll; do
    name=$(basename "$dll")
    if [[ ! -f "$OUT/$name" ]]; then
      cp "$dll" "$OUT/"
      added=1
    fi
  done < <(find "$OUT" -iname '*.exe' -o -iname '*.dll' | while read -r bin; do
             ldd "$bin" 2>/dev/null | awk '{print $3}' | grep -i "^$prefix_bin/" || true
           done | sort -u)
  [[ $added -eq 0 ]] && break
done

cat > "$OUT/README.txt" <<'EOF'
AirShow for Windows
===================

Run airshow.exe. Allow it through Windows Firewall when asked (private networks).

Nothing else is needed for discovery: AirShow answers mDNS itself (or uses
Apple's Bonjour if it is installed). If your phone or browser doesn't see
AirShow, check that the PC's network is set to Private and that AirShow is
allowed through Windows Firewall.

Project page: https://github.com/zitlem/AirShow
EOF

echo "Packaged $(find "$OUT" -type f | wc -l) files into $OUT ($(du -sh "$OUT" | cut -f1))"
