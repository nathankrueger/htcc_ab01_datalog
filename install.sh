#!/usr/bin/env bash
# install.sh — bootstrap arduino-cli and all build dependencies for data_log
#
# Usage:
#   ./install.sh            install / update everything (idempotent)
#   ./install.sh --force    reinstall cores and libraries even if already present
#
# What it does:
#   1. Install arduino-cli (if missing)
#   2. Register the CubeCell board-package URL
#   3. Fetch the board index
#   4. Install the CubeCell Development Framework core
#   5. Install the Adafruit BME280 library (+ its deps)
#   6. Run a test compile to confirm everything works

set -euo pipefail

# ─── tunables ─────────────────────────────────────────────────────────────────
ARDUINO_CLI_VER="1.4.1"
CUBECELL_URL="https://github.com/HelTecAutomation/CubeCell-Arduino/releases/download/V1.5.0/package_CubeCell_index.json"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ─── platform detection ───────────────────────────────────────────────────────
PLATFORM=""
case "$(uname -s)" in
    Darwin)  PLATFORM="macos" ;;
    Linux)   PLATFORM="linux" ;;
    *)       echo "Unsupported OS: $(uname -s)" >&2; exit 1 ;;
esac

# ─── helpers ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'  GREEN='\033[0;32m'  YELLOW='\033[1;33m'  NC='\033[0m'
log()   { printf "${GREEN}[install]${NC} %s\n" "$*"; }
warn()  { printf "${YELLOW}[warn]   ${NC} %s\n" "$*"; }
die()   { printf "${RED}[error]  ${NC} %s\n" "$*" >&2; exit 1; }

FORCE=false
[[ "${1:-}" == "--force" ]] && FORCE=true

log "Detected platform: $PLATFORM"

# ─── 1. arduino-cli ──────────────────────────────────────────────────────────
if command -v arduino-cli &>/dev/null; then
    log "arduino-cli $(arduino-cli version | awk '{print $3}') already installed"
else
    log "arduino-cli not found — downloading v${ARDUINO_CLI_VER} …"

    case "$(uname -s)-$(uname -m)" in
        Linux-x86_64)   SUFFIX="Linux_64bit"   ;;
        Linux-aarch64)  SUFFIX="Linux_ARM64"   ;;
        Linux-armv7l)   SUFFIX="Linux_ARMv7"   ;;
        Linux-armv6l)   SUFFIX="Linux_ARMv6"   ;;
        Darwin-x86_64)  SUFFIX="macOS_64bit"   ;;
        Darwin-arm64)   SUFFIX="macOS_ARM64"   ;;
        *) die "Unsupported platform: $(uname -s) $(uname -m)" ;;
    esac

    URL="https://github.com/arduino/arduino-cli/releases/download/v${ARDUINO_CLI_VER}/arduino-cli_${ARDUINO_CLI_VER}_${SUFFIX}.tar.gz"
    TMP=$(mktemp -d)
    trap "rm -rf '$TMP'" EXIT

    if command -v curl &>/dev/null; then
        curl -sLo "$TMP/cli.tar.gz" "$URL"
    elif command -v wget &>/dev/null; then
        wget -qO  "$TMP/cli.tar.gz" "$URL"
    else
        die "Neither curl nor wget found — cannot download arduino-cli"
    fi

    tar -xzf "$TMP/cli.tar.gz" -C "$TMP"

    # Try /usr/local/bin first; fall back to $HOME/bin
    if sudo cp "$TMP/arduino-cli" /usr/local/bin/ 2>/dev/null; then
        log "Installed arduino-cli → /usr/local/bin/"
    else
        mkdir -p "$HOME/bin"
        cp "$TMP/arduino-cli" "$HOME/bin/"
        export PATH="$HOME/bin:$PATH"
        warn "Installed arduino-cli → \$HOME/bin  (make sure it is on your PATH)"
    fi

    rm -rf "$TMP"
    trap - EXIT
fi

# ─── 2. board-package URL ─────────────────────────────────────────────────────
if arduino-cli config dump 2>/dev/null | grep -q "package_CubeCell_index"; then
    log "CubeCell board URL already registered"
else
    log "Registering CubeCell board-package URL …"
    arduino-cli config add board_manager.additional_urls "$CUBECELL_URL"
fi

# ─── 3. board index ───────────────────────────────────────────────────────────
log "Fetching board index …"
arduino-cli core update-index

# ─── 4. CubeCell core ────────────────────────────────────────────────────────
if ! $FORCE && arduino-cli core list 2>/dev/null | grep -q "CubeCell:CubeCell"; then
    log "CubeCell Development Framework already installed"
else
    log "Installing CubeCell Development Framework …"
    arduino-cli core install CubeCell:CubeCell
fi

# ─── 5. libraries ─────────────────────────────────────────────────────────────
# arduino-cli resolves and installs transitive dependencies automatically.
# Adafruit BME280 pulls in Adafruit BusIO and Adafruit Unified Sensor.
if ! $FORCE && arduino-cli lib list 2>/dev/null | grep -q "Adafruit BME280"; then
    log "Adafruit BME280 Library already installed"
else
    log "Installing Adafruit BME280 Library (+ dependencies) …"
    arduino-cli lib install "Adafruit BME280 Library"
fi

if ! $FORCE && arduino-cli lib list 2>/dev/null | grep -q "TinyGPSPlus"; then
    log "TinyGPSPlus Library already installed"
else
    log "Installing TinyGPSPlus Library …"
    arduino-cli lib install "TinyGPSPlus"
fi

# ─── 6. verify ────────────────────────────────────────────────────────────────
log ""
log "Installed cores:"
arduino-cli core list
echo
log "Installed libraries:"
arduino-cli lib list
echo

log "Running test compile …"
cd "$SCRIPT_DIR"
arduino-cli compile \
    --fqbn "CubeCell:CubeCell:CubeCell-Board-V2:LORAWAN_REGION=9" \
    --build-path build/data_log \
    --build-property "compiler.c.extra_flags=-I$SCRIPT_DIR/shared" \
    --build-property "compiler.cpp.extra_flags=-I$SCRIPT_DIR/shared" \
    data_log/data_log.ino

log ""
log "Setup complete. Run 'make' to compile, 'make upload' to flash."

# ─── platform-specific tips ──────────────────────────────────────────────────
if [[ "$PLATFORM" == "macos" ]]; then
    log ""
    log "macOS tips:"
    log "  - Serial port: ls /dev/cu.usbserial-* (use with make PORT=...)"
    log "  - USB devices work directly — no usbipd required"
elif [[ "$PLATFORM" == "linux" ]]; then
    log ""
    log "Linux/WSL tips:"
    log "  - Default serial port: /dev/ttyUSB0"
    log "  - For WSL USB passthrough, see README.md"
fi
