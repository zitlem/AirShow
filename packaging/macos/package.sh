#!/usr/bin/env bash
# Assemble a self-contained AirShow.app (and .dmg) from a Homebrew build:
# Qt via macdeployqt, the GStreamer plugins AirShow uses, and every library they
# depend on, rewritten to load from inside the bundle. Run at the repo root:
#   packaging/macos/package.sh [build-dir] [output-dir] [version]
set -euo pipefail

BUILD_DIR=${1:-build}
OUT=${2:-dist}
VERSION=${3:-0.0.0}
APP="$OUT/AirShow.app"
GST_PLUGINS_DIR="$(brew --prefix gstreamer)/lib/gstreamer-1.0"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/PlugIns/gstreamer"

cp "$BUILD_DIR/airshow" "$APP/Contents/MacOS/"
cp -R "$BUILD_DIR/resources" "$APP/Contents/MacOS/"     # looked up next to the binary
cp resources/icons/airshow.icns "$APP/Contents/Resources/"
sed "s/@VERSION@/$VERSION/g" packaging/macos/Info.plist > "$APP/Contents/Info.plist"

# GStreamer plugins used by the AirPlay, Cast, DLNA and Miracast pipelines.
# HTTP for DLNA uses curl: the soup plugin dlopen()s libsoup by bare name, which
# can't resolve to a library inside the bundle.
PLUGINS=(
  coreelements app playback typefindfunctions autodetect
  videoconvertscale videotestsrc videorate
  audiotestsrc audioconvert audioresample volume osxaudio
  applemedia libav vpx opus
  videoparsersbad audioparsers dvdlpcmdec
  mpegtsdemux rtp rtpmanager udp tcp curl
  isomp4 matroska ogg vorbis flac wavparse id3demux icydemux mpg123
)
extra=()
for p in "${PLUGINS[@]}"; do
  f="$GST_PLUGINS_DIR/libgst$p.dylib"
  if [[ -f "$f" ]]; then
    cp "$f" "$APP/Contents/PlugIns/gstreamer/"
    extra+=("-executable=$APP/Contents/PlugIns/gstreamer/libgst$p.dylib")
  else
    echo "note: GStreamer plugin '$p' not installed — skipped"
  fi
done

# Qt frameworks + QML, plus every non-system dylib the binary and the plugins
# load, copied into Contents/Frameworks with install names rewritten.
macdeployqt "$APP" -qmldir=qml -always-overwrite "${extra[@]}"

# Plugins still carry their Homebrew install name as their own ID; harmless for
# dlopen(), but reset it so nothing in the bundle names the Homebrew prefix.
for f in "$APP"/Contents/PlugIns/gstreamer/*.dylib; do
  install_name_tool -id "@loader_path/$(basename "$f")" "$f"
done

# macdeployqt copies whole QML import trees (e.g. QtQuick/Pdf, Scene3D,
# VirtualKeyboard) whose frameworks AirShow doesn't use and that aren't bundled.
# Drop any Qt plugin that links a framework missing from the bundle.
find "$APP/Contents/PlugIns" -name '*.dylib' ! -path '*/gstreamer/*' | while read -r plugin; do
  for fw in $(otool -L "$plugin" | grep -oE '@rpath/Qt[A-Za-z0-9]+\.framework' | sed 's|@rpath/||'); do
    if [[ ! -d "$APP/Contents/Frameworks/$fw" ]]; then
      echo "pruning ${plugin#"$APP"/} (needs $fw)"
      rm -f "$plugin"
      break
    fi
  done
done
# ...and the Resources/qml symlinks that pointed at them (codesign rejects dangling links).
find "$APP" -type l ! -exec test -e {} \; -print -delete

# Homebrew binaries also locate libraries through absolute LC_RPATH entries
# (e.g. Rust GStreamer plugins -> @rpath/libgstreamer, libupnp -> @rpath/libixml).
# Left in place, the loader picks up a second copy from Homebrew when it's
# installed (duplicate GStreamer -> crash) and fails where it isn't. Replace them
# with the bundle's Frameworks directory.
machos=$(mktemp)
find "$APP/Contents" -type f -exec sh -c 'file -b "$1" | grep -q Mach-O' _ {} \; -print > "$machos"
while IFS= read -r f <&3; do
  for rp in $(otool -l "$f" | awk '/cmd LC_RPATH/{getline; getline; print $2}'); do
    case "$rp" in
      @*) ;;
      *) install_name_tool -delete_rpath "$rp" "$f" 2>/dev/null ;;
    esac
  done
  if otool -L "$f" | grep -q '@rpath/' &&
     ! otool -l "$f" | grep -q 'path @executable_path/../Frameworks '; then
    install_name_tool -add_rpath @executable_path/../Frameworks "$f" 2>/dev/null
  fi
done 3< "$machos"
bad_rpaths=$(while IFS= read -r f <&3; do
               otool -l "$f" | awk -v f="$f" '/cmd LC_RPATH/{getline; getline; if ($2 !~ /^@/) print f": "$2}'
             done 3< "$machos")
rm -f "$machos"
if [[ -n "$bad_rpaths" ]]; then
  echo "error: absolute rpaths left in bundle:" >&2
  echo "$bad_rpaths" >&2
  exit 1
fi

# Nothing inside the bundle may still point at the Homebrew prefix.
leftovers=$(find "$APP" -type f \( -name '*.dylib' -o -perm -u+x \) -print0 |
            xargs -0 otool -L 2>/dev/null | grep -E "^\s+$(brew --prefix)/" | sort -u || true)
if [[ -n "$leftovers" ]]; then
  echo "error: bundle still references Homebrew libraries:" >&2
  echo "$leftovers" >&2
  exit 1
fi

# Apple Silicon refuses unsigned code: ad-hoc sign every binary, then the app.
find "$APP/Contents" -type f \( -name '*.dylib' -o -name '*.so' \) -exec codesign --force --sign - {} \;
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

ARCH=$(uname -m)
rm -f "$OUT/AirShow-macos-$ARCH.dmg"
hdiutil create -volname AirShow -srcfolder "$APP" -ov -format UDZO "$OUT/AirShow-macos-$ARCH.dmg" >/dev/null
echo "Packaged $APP ($(du -sh "$APP" | cut -f1)) and $OUT/AirShow-macos-$ARCH.dmg"
