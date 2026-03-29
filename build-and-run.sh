#!/usr/bin/env bash
# MyAirShow — Build and Run (Linux / macOS)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Detect platform
case "$(uname -s)" in
    Linux*)  PRESET="linux-debug" ;;
    Darwin*) PRESET="macos-debug" ;;
    *)       echo "Unsupported platform: $(uname -s)"; exit 1 ;;
esac

BUILD_DIR="build/${PRESET##*-}-debug"
# Preset binaryDir uses: build/linux-debug or build/macos-debug
BUILD_DIR="build/$PRESET"

echo "=== MyAirShow Build & Run ==="
echo "Platform: $(uname -s)"
echo "Preset:   $PRESET"
echo "Build:    $BUILD_DIR"
echo ""

# --- Dependencies check ---
check_cmd() {
    if ! command -v "$1" &>/dev/null; then
        echo "ERROR: '$1' not found. $2"
        return 1
    fi
}

check_cmd cmake "Install cmake >= 3.28"
check_cmd ninja "Install ninja-build (apt install ninja-build / brew install ninja / pip install ninja)"

if [ "$PRESET" = "linux-debug" ]; then
    echo "Checking Linux dependencies..."
    check_cmd pkg-config "Install pkg-config"

    MISSING=""
    pkg-config --exists gstreamer-1.0 2>/dev/null || MISSING="$MISSING gstreamer1.0-dev"
    pkg-config --exists Qt6Core 2>/dev/null || MISSING="$MISSING qt6-base-dev qt6-declarative-dev"
    pkg-config --exists openssl 2>/dev/null || MISSING="$MISSING libssl-dev"

    if [ -n "$MISSING" ]; then
        echo ""
        echo "Missing packages (install with your package manager):"
        echo "  sudo apt install$MISSING gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav libgstreamer-plugins-bad1.0-dev qt6-multimedia-dev libavahi-client-dev libupnp-dev libplist-dev"
        echo ""
        echo "Or on Fedora:"
        echo "  sudo dnf install$MISSING gstreamer1-plugins-base-devel qt6-qtdeclarative-devel avahi-devel libupnp-devel libplist-devel openssl-devel"
        exit 1
    fi
    echo "  All dependencies found."

elif [ "$PRESET" = "macos-debug" ]; then
    echo "Checking macOS dependencies..."
    check_cmd brew "Install Homebrew: https://brew.sh"

    MISSING=""
    brew list qt@6 &>/dev/null || MISSING="$MISSING qt@6"
    brew list gstreamer &>/dev/null || MISSING="$MISSING gstreamer"
    brew list openssl &>/dev/null || MISSING="$MISSING openssl"
    brew list libplist &>/dev/null || MISSING="$MISSING libplist"
    brew list libupnp &>/dev/null || MISSING="$MISSING libupnp"

    if [ -n "$MISSING" ]; then
        echo ""
        echo "Missing Homebrew packages:"
        echo "  brew install$MISSING"
        exit 1
    fi
    echo "  All dependencies found."
fi

echo ""

# --- Configure ---
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    echo "=== Configuring ($PRESET) ==="
    cmake --preset "$PRESET"
    echo ""
fi

# --- Build ---
echo "=== Building ==="
cmake --build "$BUILD_DIR" --target myairshow -j "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
echo ""

# --- Run ---
BINARY="$BUILD_DIR/myairshow"
if [ ! -f "$BINARY" ]; then
    echo "ERROR: Binary not found at $BINARY"
    exit 1
fi

echo "=== Running MyAirShow ==="
echo "Press Ctrl+C to stop."
echo ""
exec "$BINARY" "$@"
