#!/usr/bin/env bash
# AirShow — Build and Run in Windowed Mode (Linux / macOS)
# Same as build-and-run.sh but passes --windowed to the binary.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
exec "$SCRIPT_DIR/build-and-run.sh" --windowed "$@"
