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

# ─── Radio Defaults ─────────────────────────────────────────────────────
LORAWAN_REGION   = 9           # 9=US915 (see Makefile for full list)
TX_OUTPUT_POWER  = 14          # dBm (-17 to 22)
RX_DUTY_PERCENT_DEFAULT = 90   # 0-100, % of cycle spent listening

# ─── Sensors ────────────────────────────────────────────────────────────
# Space-separated list of sensors to enable: bme280 batt
SENSORS            = bme280
# SENSORS          = bme280 batt   # uncomment to add battery reporting

# Per-sensor sample intervals (seconds, 1-32767)
BME280_RATE_SEC_DEFAULT = 30
BATT_RATE_SEC_DEFAULT   = 60

# ─── One-Time Setup (uncomment, upload once, then re-comment) ──────────
# WRITE_NODE_ID    = ab01        # Writes node ID to EEPROM
# UPDATE_CFG       = 1           # Forces compile-time defaults to EEPROM
