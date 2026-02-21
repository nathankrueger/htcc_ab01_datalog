# ─── Per-Node Configuration ──────────────────────────────────────────────
#
# Values here override Makefile defaults. CLI overrides still win:
#   make upload TX_OUTPUT_POWER=20   <- overrides anything in this file
#
# Comment out the lines you want to assume their defaults.
# ─────────────────────────────────────────────────────────────────────────

# ─── Node Identity ──────────────────────────────────────────────────────
NODE_VERSION     = 1

# ─── Build Options ──────────────────────────────────────────────────────
DEBUG            = 0           # 0=quiet, 1=Serial.printf debug output
CMD_DEBUG        = 0           # 0=quiet, 1=command/ACK debug trace
LED_BRIGHTNESS   = 16          # 0-255, NeoPixel brightness
LED_ORDER        = GRB         # NeoPixel color order
CYCLE_PERIOD_MS  = 5000        # Main loop cycle time (ms)

# ─── Radio Defaults ─────────────────────────────────────────────────────
LORAWAN_REGION   = 9                # 9=US915 (see Makefile for full list)
TX_OUTPUT_POWER  = 14               # dBm (-17 to 22)
RX_DUTY_PERCENT_DEFAULT = 90        # 0-100, % of cycle spent listening
SPREADING_FACTOR_DEFAULT = 7        # LoRa SF (7-12, higher = longer range)
BANDWIDTH_DEFAULT = 0               # LoRa BW (0=125kHz, 1=250kHz, 2=500kHz)
N2G_FREQUENCY_DEFAULT = 915000000   # Node-to-Gateway freq (Hz)
G2N_FREQUENCY_DEFAULT = 915500000   # Gateway-to-Node freq (Hz)
BROADCAST_ACK_JITTER_DEFAULT = 1000 # ACK jitter (ms, 0=disable)

# ─── Sensors ────────────────────────────────────────────────────────────
# Space-separated list of sensors to enable: bme280 batt
SENSORS          = bme280
# SENSORS          = bme280 batt

# Per-sensor sample intervals (seconds, 1-32767)
BME280_RATE_SEC_DEFAULT = 30
BATT_RATE_SEC_DEFAULT   = 60

# ─── One-Time Setup (uncomment, upload once, then re-comment) ──────────
# WRITE_NODE_ID    = ab01        # Writes node ID to EEPROM
# UPDATE_CFG       = 1           # Forces compile-time defaults to EEPROM
