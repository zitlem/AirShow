#!/usr/bin/env bash
# Build a single-file AirShow AppImage from a Linux build using linuxdeploy with
# its Qt and GStreamer plugins (bundles Qt, QML, GStreamer plugins and their
# libraries; the GStreamer plugin's launcher hook points GStreamer at them).
# Build on the oldest distro you want to support — the AppImage needs a glibc at
# least as new as the build machine's. Run at the repo root:
#   packaging/linux/package.sh [build-dir] [output-dir] [version]
set -euo pipefail

BUILD_DIR=${1:-build}
OUT=${2:-dist}
VERSION=${3:-0.0.0}
TOOLS=${TOOLS_DIR:-$OUT/tools}
APPDIR="$OUT/AppDir"

mkdir -p "$TOOLS"
fetch() {
  if [[ ! -f "$TOOLS/$1" ]]; then curl -fsSL -o "$TOOLS/$1" "$2"; fi
  chmod +x "$TOOLS/$1"
}
fetch linuxdeploy-x86_64.AppImage \
  https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch linuxdeploy-plugin-qt-x86_64.AppImage \
  https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
fetch linuxdeploy-plugin-gstreamer.sh \
  https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gstreamer/master/linuxdeploy-plugin-gstreamer.sh

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
cp -r "$BUILD_DIR/resources" "$APPDIR/usr/bin/"     # looked up next to the binary

export PATH="$TOOLS:$PATH"
export APPIMAGE_EXTRACT_AND_RUN=1                    # no FUSE needed on CI
export QML_SOURCES_PATHS="$PWD/qml"
export GSTREAMER_INCLUDE_BAD_PLUGINS=1
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
export OUTPUT="$OUT/AirShow-linux-x86_64.AppImage"

linuxdeploy-x86_64.AppImage \
  --appdir "$APPDIR" \
  --executable "$BUILD_DIR/airshow" \
  --desktop-file packaging/linux/airshow.desktop \
  --icon-file resources/icons/airshow.png \
  --plugin qt --plugin gstreamer \
  --output appimage

echo "Packaged $OUTPUT ($(du -sh "$OUTPUT" | cut -f1))"
