# data_log

Arduino sketch for the Heltec CubeCell HTCC-AB01. Reads a BME280 sensor
(temperature, pressure, humidity) and broadcasts compact JSON packets over
LoRa to a gateway running `gateway_server.py` on a Raspberry Pi Zero 2 W.

See: https://heltec.org/project/htcc-ab01-v2/
See: https://github.com/HelTecAutomation/CubeCell-Arduino/tags

## Architecture

```
CubeCell (this sketch)
  │  LoRa broadcast (JSON + CRC-32)
  ▼
Gateway (RPi Zero 2 W)        ── TCP ──▶  Pi5 Dashboard
  gateway_server.py
```

Packets use the compact protocol defined in `utils/protocol.py` in the
`data_log` project.  CRC-32 is computed over sorted-key JSON (matching
Python `zlib.crc32`) so the gateway can verify integrity without
trusting wire key order.

## Quick Start

```sh
# 1. Bootstrap arduino-cli + CubeCell core + libraries (run once)
./install.sh

# 2. Compile
make

# 3. Upload to device  (auto-attaches USB on WSL — see below)
make upload

# 4. Open serial monitor
make monitor
```
* For Windows / WSL usage, install the serial driver: https://www.silabs.com/software-and-tools/usb-to-uart-bridge-vcp-drivers?tab=downloads

## Configuration

### Compile-time defaults

Overridable via Makefile variables (which become `-D` compiler flags):

| Define                    | Default | Description                              |
|---------------------------|---------|------------------------------------------|
| `NODE_ID`                 | `ab01`  | Unique node name in the sensor network   |
| `CYCLE_PERIOD_MS`         | `5000`  | Milliseconds per TX/RX cycle             |
| `TX_OUTPUT_POWER`         | 14 dBm  | Transmit power (-17 to 22)               |
| `RX_DUTY_PERCENT_DEFAULT` | `90`    | % of cycle spent listening for commands  |
| `SPREADING_FACTOR_DEFAULT`| `7`     | LoRa spreading factor (SF7-SF12)         |
| `BANDWIDTH_DEFAULT`       | `0`     | LoRa bandwidth (0=125kHz, 1=250kHz, 2=500kHz) |
| `LED_BRIGHTNESS`          | `16`    | NeoPixel brightness (0-255)              |
| `DEBUG`                   | `1`     | Enable serial debug output               |

LoRaWAN region can be set at build time (does not affect this sketch's
plain LoRa usage, but the CubeCell SDK requires it):

```sh
make upload LORAWAN_REGION=6   # EU868
```

### Runtime parameters

Several parameters can be changed at runtime via LoRa commands without
recompiling. Use `setparam <name> <value>` to change, `savecfg` to
persist to EEPROM:

| Param    | Type   | Range    | Description                              |
|----------|--------|----------|------------------------------------------|
| `txpwr`  | int8   | -17..22  | TX power in dBm                          |
| `rxduty` | uint8  | 0..100   | RX duty cycle percentage                 |
| `sf`     | uint8  | 7..12    | Spreading factor                         |
| `bw`     | uint8  | 0..2     | Bandwidth (0=125kHz, 1=250kHz, 2=500kHz) |
| `nodeid` | string | —        | Node ID (read-only)                      |
| `nodev`  | uint16 | —        | Node version (read-only)                 |

### EEPROM config versioning

`NodeConfig` is stored in EEPROM with a two-field validity check:

- **`CFG_MAGIC`** (0xCF) — Fixed sentinel that detects blank EEPROM. Never changes.
- **`CFG_VERSION`** (currently 1) — Struct layout version. **Bump whenever
  fields are added/removed/reordered in `NodeConfig`** (in `shared/config_types.h`).

When the firmware boots and either field doesn't match, compile-time
defaults are loaded automatically. This means adding a new EEPROM-backed
parameter resets all nodes to defaults on first boot with the new firmware.

## WSL USB Passthrough (Windows)

The CubeCell connects via USB-to-serial.  On WSL 2 this requires
`usbipd-win` to forward the USB device into the Linux VM.

### One-time setup (bind)

Open PowerShell **as Administrator** and find your device:

```powershell
usbipd list
# Look for the CP210x / CH340 serial adapter — note its BUSID (e.g. 1-2)
```

Bind it so usbipd knows to make it available to WSL.  This persists
across reboots — you only do it once:

```powershell
usbipd bind --busid 1-2 --force
```

If your bus ID is not `1-2`, update `USBIPD_BUSID` in the Makefile
(or pass it on the command line):

```sh
make upload USBIPD_BUSID=2-3
```

### Per-session attach (automatic)

`make upload` checks whether `$(PORT)` exists.  If the device is not
yet visible, it calls `usbipd attach` automatically via `powershell.exe`
and waits up to 5 seconds for it to appear.  No manual steps needed
after the one-time bind.

If you still see an error after attach, verify the device is plugged in
and run `usbipd list` in PowerShell to confirm its state is **Shared**.

## Packet Format

Each LoRa packet is a single JSON object (≤ 250 bytes):

```json
{"n":"ab01","r":[{"k":"Temperature","s":0,"u":"\u00b0F","v":74.2},
                 {"k":"Pressure","s":0,"u":"hPa","v":918.1},
                 {"k":"Humidity","s":0,"u":"%","v":27.4}],"t":0,"c":"ac74c66a"}
```

| Key | Meaning                                      |
|-----|----------------------------------------------|
| `n` | Node ID                                      |
| `t` | Timestamp (Unix epoch; currently hardcoded 0)|
| `r` | Array of readings                            |
| `s` | Sensor class ID (registry in `sensors/__init__.py`) |
| `k` | Reading name                                 |
| `u` | Units (non-ASCII escaped for CRC stability) |
| `v` | Value                                        |
| `c` | CRC-32 over the JSON body without `"c"`      |

## Known Quirks

**ASR650x TX-FIFO drift** — The integrated radio on the CubeCell
silently drops the first 4 bytes of every LoRa payload after the
initial transmission.  The sketch works around this by prepending 4
ASCII space bytes that get eaten by the radio; `json.loads` on the
gateway ignores the leading whitespace.

**CubeCell `snprintf` trailing zeros** — `%g` does not strip trailing
zeros on this platform.  A manual `fmtVal()` function strips them so
the serialized floats match Python's `json.dumps` round-trip output,
keeping the CRC stable.

**Timestamp is zero** — NTP or an RTC module would be needed to send
real timestamps.  The gateway passes the value through unchanged.
