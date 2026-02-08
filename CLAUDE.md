# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Multi-sketch Arduino project for the Heltec CubeCell HTCC-AB01 (ASR650x MCU). The default sketch reads BME280 sensor data (temperature, pressure, humidity) and broadcasts compact JSON packets over LoRa to a gateway on a Raspberry Pi Zero 2 W. A second sketch (`range_test`) is a field tool for measuring LoRa reception range using an SSD1306 OLED display and NEO-6M GPS module. Both sketches use a dual-channel LoRa architecture with shared protocol code.

See: https://heltec.org/project/htcc-ab01-v2/

## Build Commands

```sh
./install.sh              # One-time bootstrap: arduino-cli + CubeCell core + libraries
make                      # Compile default sketch (data_log)
make upload               # Compile + upload (auto-attaches USB on WSL)
make monitor              # Serial monitor at 115200 baud
make clean                # Remove build artifacts for current sketch
make clean-all            # Remove all build artifacts
```

Select which sketch to build with the `SKETCH` variable:
```sh
make SKETCH=range_test           # Compile range test sketch
make upload SKETCH=range_test    # Upload range test sketch
```

Build-time parameters are passed via Makefile variables:
```sh
make upload NODE_ID=ab02 SEND_INTERVAL_MS=10000 LED_BRIGHTNESS=64 DEBUG=0
make upload LORAWAN_REGION=6          # EU868
make upload PORT=/dev/ttyUSB1         # Different serial port
make upload USBIPD_BUSID=2-3         # WSL USB bus ID
make VERBOSE=1                        # Detailed compilation output
```

There are no tests. The build system uses `arduino-cli` (not PlatformIO).

## Platform Support

The Makefile and install.sh must work on both **WSL Ubuntu** and **macOS**. The Makefile uses `uname -s` for platform detection (serial port defaults, USB passthrough). Any changes to build scripts or install logic must be verified against both platforms.

## Companion Repository

The gateway/dashboard project (`data_log`) is typically located at `../data_log` relative to this repo. The gateway server is at `../data_log/gateway_server.py`. Protocol changes (packet format, CRC, sensor class IDs) often require coordinated changes across both repos. If `../data_log` doesn't exist, ask the user where their `data_log` repo is located.

## Architecture

```
CubeCell Node (this sketch)
  │  LoRa N2G 915 MHz ────► Sensor data + ACKs
  │  LoRa G2N 915.5 MHz ◄── Commands
  ▼
Gateway (RPi Zero 2 W)  ── TCP ──►  Pi5 Dashboard
  ../data_log/gateway_server.py
```

**Dual-channel LoRa:** N2G (node-to-gateway, 915 MHz) carries sensor packets and ACKs. G2N (gateway-to-node, 915.5 MHz) carries command packets. This avoids TX/RX collision on a single frequency.

**Main loop cycle** (`CYCLE_PERIOD_MS`, default 5s):
1. Read BME280 sensor → build JSON packet with CRC-32
2. TX on N2G frequency
3. Switch to G2N frequency, listen for commands for up to `rxDutyPercent`% of remaining cycle
4. Dispatch any received commands, send ACK on N2G
5. Sleep remaining time

## Key Files

- **data_log/data_log.ino** — Datalog sketch: setup, loop, LoRa callbacks, command handlers (`ping`, `blink`, `rxduty`), LED control, BME280 reading
- **range_test/range_test.ino** — Range test sketch: listens for gateway pings on G2N, displays RSSI on SSD1306 OLED, reads GPS from NEO-6M, sends GPS sensor packet on N2G
- **shared/packets.h** — All protocol logic: CRC-32 (matches Python `zlib.crc32`), JSON packet construction/parsing, command registry (`cmdRegister`/`cmdDispatch`), Reading struct, `fmtVal()` float formatting. Shared by both sketches via `-I shared/` include path.
- **Makefile** — Build system with platform detection (macOS/Linux/WSL), `arduino-cli` invocation, compiler defines, USB passthrough, multi-sketch support (`SKETCH` variable)
- **install.sh** — Bootstrap script for arduino-cli, CubeCell board core, and libraries (BME280, TinyGPSPlus)

## Protocol Details

Packets are compact JSON (≤ 250 bytes). CRC-32 is computed over alphabetically-sorted JSON keys (excluding the `"c"` field) to match Python `zlib.crc32()` for gateway verification. If sensor data exceeds 250 bytes, `buildSensorPacket()` auto-splits readings across multiple packets.

Three packet types: **sensor** (`"r"` array of readings), **command** (`"t":"cmd"`), and **ack** (`"t":"ack"`).

## Hardware Quirks to Know

- **ASR650x TX-FIFO drift:** First 4 bytes of LoRa payload are silently dropped after initial TX. Workaround: 4 leading space bytes prepended (gateway's `json.loads` ignores whitespace).
- **CubeCell `snprintf %g`:** Doesn't strip trailing zeros. Custom `fmtVal()` in packets.h handles this so CRC stays stable between node and gateway.
- **Sensor class IDs** (the `"s"` field) are derived from alphabetical sort of Python class names in the companion `data_log` project's `sensors/__init__.py`.

## Configuration Defines

All configurable via Makefile variables (which become `-D` compiler flags) or `#define` in the sketch:
- `NODE_ID` (string, default `"ab01"`) — node identifier
- `SEND_INTERVAL_MS` / `CYCLE_PERIOD_MS` (default 5000) — cycle period
- `LED_BRIGHTNESS` (0-255, default 64) — NeoPixel brightness
- `DEBUG` (0 or 1, default 1) — enables `Serial.printf` debug output via `DBG()`/`DBGLN()`/`DBGP()` macros
- `RX_DUTY_PERCENT_DEFAULT` (0-100, default 90) — fraction of cycle spent listening for commands (datalog only)

## Range Test Tool

Field testing tool for measuring LoRa reception range. The CubeCell node receives gateway pings, shows RSSI on an SSD1306 OLED, and sends GPS coordinates back.

**Node side** (`range_test/range_test.ino`):
- Listens on G2N (915.5 MHz) for ping commands
- Displays RSSI in large font on SSD1306 (128x64, I2C 0x3C, SDA/SCL)
- Blinks red NeoPixel LED for 1s on each received ping
- Reads GPS from NEO-6M via Serial1 (9600 baud, RX=P3_0, TX=P3_1)
- Sends ACK + GPS sensor packet on N2G (915.0 MHz)
- Sensor class ID 2 for GPS readings (Latitude, Longitude, Satellites, RSSI)

**Gateway side** (`../data_log/scripts/range_test.py`):
- Sends periodic ping commands on G2N
- Logs responses with RSSI, GPS, timestamps
- Frequency-hops between G2N (send) and N2G (receive)
- CSV output for post-processing

```sh
# On gateway Pi:
python3 ../data_log/scripts/range_test.py --node ab01 --interval 5
python3 ../data_log/scripts/range_test.py --csv results.csv --duration 300
```
