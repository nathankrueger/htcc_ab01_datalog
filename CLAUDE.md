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

Run native C unit tests (no Arduino dependencies):
```sh
make test                         # Build and run tests (output in build/tests/)
```

The build system uses `arduino-cli` (not PlatformIO).

## Platform Support

The Makefile and install.sh must work on both **WSL Ubuntu** and **macOS**. The Makefile uses `uname -s` for platform detection (serial port defaults, USB passthrough). Any changes to build scripts or install logic must be verified against both platforms.

## Companion Repository

The companion `data_log` repo is typically at `../data_log` relative to this repo. If `../data_log` doesn't exist, ask the user where their `data_log` repo is located.

Key files in the companion repo:
- **`gateway/server.py`** — Gateway server on the Raspberry Pi. Receives LoRa packets from nodes, verifies CRC, forwards to the dashboard over TCP. Protocol changes (packet format, CRC, JSON key ordering, sensor class IDs) require coordinated changes here.
- **`node/data_log.py`** — Python equivalent of `data_log/data_log.ino`. Runs on a Raspberry Pi node as a software alternative to the CubeCell hardware node. New features or enhancements added to `data_log.ino` should also be implemented in `data_log.py` to keep both node implementations in sync.

When making significant changes to this repo (new commands, protocol changes, new sensor types, param registry changes), check whether the companion repo needs matching updates.

## Architecture

```
CubeCell Node (this sketch)
  │  LoRa N2G 915 MHz ────► Sensor data + ACKs
  │  LoRa G2N 915.5 MHz ◄── Commands
  ▼
Gateway (RPi Zero 2 W)  ── TCP ──►  Pi5 Dashboard
  ../data_log/gateway/server.py
```

**Dual-channel LoRa:** N2G (node-to-gateway, 915 MHz) carries sensor packets and ACKs. G2N (gateway-to-node, 915.5 MHz) carries command packets. This avoids TX/RX collision on a single frequency.

**Main loop cycle** (`CYCLE_PERIOD_MS`, default 5s):
1. Read BME280 sensor → build JSON packet with CRC-32
2. TX on N2G frequency
3. Switch to G2N frequency, listen for commands for up to `rxDutyPercent`% of remaining cycle
4. Dispatch any received commands, send ACK on N2G
5. Sleep remaining time

## Key Files

- **data_log/data_log.ino** — Datalog sketch: setup, loop, LoRa callbacks, command handlers, param table, LED control, BME280 reading
- **range_test/range_test.ino** — Range test sketch: listens for gateway pings on G2N, displays RSSI on SSD1306 OLED, reads GPS from NEO-6M, sends GPS sensor packet on N2G
- **shared/packets.h** — Protocol logic: CRC-32 (matches Python `zlib.crc32`), JSON packet construction/parsing, command registry (`cmdRegister`/`cmdDispatch`), Reading struct, `fmtVal()` float formatting
- **shared/params.h** — Generic parameter registry: `ParamDef` table, `paramGet`/`paramSet`/`paramsList`/`cmdsList`/`paramsSyncToConfig`. All `static inline`, testable without Arduino
- **shared/config_types.h** — `NodeConfig` struct and EEPROM versioning constants (`CFG_MAGIC`, `CFG_VERSION`). Separated from config.h so unit tests can include it without `<EEPROM.h>`
- **shared/config.h** — EEPROM persistence: `cfgLoad`/`cfgSave`/`cfgDefaults`. Requires Arduino `<EEPROM.h>`
- **tests/** — Native C unit tests compiled with `gcc -std=c11`. Run via `make test`
- **Makefile** — Build system with platform detection (macOS/Linux/WSL), `arduino-cli` invocation, compiler defines, USB passthrough, multi-sketch support (`SKETCH` variable)
- **install.sh** — Bootstrap script for arduino-cli, CubeCell board core, and libraries (BME280, TinyGPSPlus)

## Protocol Details

Packets are compact JSON (≤ 250 bytes). CRC-32 is computed over alphabetically-sorted JSON keys (excluding the `"c"` field) to match Python `zlib.crc32()` for gateway verification. If sensor data exceeds 250 bytes, `buildSensorPacket()` auto-splits readings across multiple packets.

Three packet types: **sensor** (`"r"` array of readings), **command** (`"t":"cmd"`), and **ack** (`"t":"ack"`).

## Hardware Quirks to Know

- **ASR650x TX-FIFO drift:** First 4 bytes of LoRa payload are silently dropped after initial TX. Workaround: 4 leading space bytes prepended (gateway's `json.loads` ignores whitespace).
- **CubeCell `snprintf %g`:** Doesn't strip trailing zeros. Custom `fmtVal()` in packets.h handles this so CRC stays stable between node and gateway.
- **Sensor class IDs** (the `"s"` field) are derived from alphabetical sort of Python class names in the companion `data_log` project's `sensors/__init__.py`.

## EEPROM Config Versioning

`NodeConfig` is persisted to EEPROM with a two-field validity check in `config_types.h`:

- **`CFG_MAGIC`** (0xCF) — Fixed sentinel. Detects blank/uninitialized EEPROM. **Never changes.**
- **`CFG_VERSION`** (currently 1) — Struct layout version. **Bump this whenever you add, remove, or reorder fields in `NodeConfig`.**

`cfgLoad()` checks both: if either doesn't match, the EEPROM data is treated as invalid and compile-time defaults are loaded. This means deploying firmware with a new `CFG_VERSION` automatically resets all nodes to defaults — any previously saved txpwr/rxduty/sf/bw customizations will be lost. That's intentional: the old EEPROM bytes no longer map to the right fields.

**When to bump `CFG_VERSION`:**
- Adding a new field to `NodeConfig` (e.g., a new tunable parameter)
- Removing or reordering existing fields
- Changing a field's type or size

**When NOT to bump:**
- Adding a new command handler (no struct change)
- Changing default values in `config.h` (struct layout unchanged)
- Changes to `params.h` or `packets.h` that don't touch `NodeConfig`

## Configuration Defines

All configurable via Makefile variables (which become `-D` compiler flags) or `#define` in the sketch:
- `NODE_ID` (string, default `"ab01"`) — node identifier
- `SEND_INTERVAL_MS` / `CYCLE_PERIOD_MS` (default 5000) — cycle period
- `LED_BRIGHTNESS` (0-255, default 64) — NeoPixel brightness
- `DEBUG` (0 or 1, default 1) — enables `Serial.printf` debug output via `DBG()`/`DBGLN()`/`DBGP()` macros
- `RX_DUTY_PERCENT_DEFAULT` (0-100, default 90) — fraction of cycle spent listening for commands (datalog only)

## Parameter Registry

Runtime-tunable parameters are defined as a `ParamDef` table in `data_log.ino`. Adding a new parameter is a single row addition — no new command handlers needed.

Commands: `getparam <name>`, `setparam <name> <value>`, `getparams [offset]`, `getcmds [offset]`. `setparam` changes runtime only; `savecfg` persists to EEPROM via `paramsSyncToConfig()`.

The param table **must** be in alphabetical order by name (JSON key ordering for CRC matching). List responses use self-terminating pagination with a `"m"` (more) flag for the 171-byte ACK payload limit.

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
