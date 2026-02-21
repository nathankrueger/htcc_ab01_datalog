# Arduino CLI Makefile for CubeCell HTCC-AB01 — Data Log Sketch
#
# Usage:
#   make                           # compile data_log sketch
#   make upload                    # compile + upload  (auto-attaches USB on WSL)
#   make monitor                   # open serial monitor
#   make clean                     # remove build artifacts
#   make clean-all                 # remove all build artifacts (all sketches + tests)
#   make test                      # run native C unit tests
#   make -C range_test             # compile range test sketch (separate Makefile)
#
# Override defaults on the command line, e.g.:
#   make LORAWAN_REGION=6                # switch to EU868
#   make upload PORT=/dev/ttyUSB1        # use a different serial port
#   make upload USBIPD_BUSID=2-3         # use a different USB bus ID (WSL only)
#   make VERBOSE=1                       # show detailed compilation output

# Load per-node config (if present). CLI overrides still win.
-include node_config.mk

FQBN       = CubeCell:CubeCell:CubeCell-Board-V2
BUILD_DIR  = build/data_log

# ─── Platform detection ──────────────────────────────────────────────────────
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  # macOS: auto-detect USB serial port if not specified
  PORT ?= $(shell ls /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART /dev/cu.wchusbserial* 2>/dev/null | head -1)
  ifeq ($(PORT),)
    # No device found — set a placeholder that will trigger a helpful error
    PORT = __no_device_found__
  endif
else
  # Linux (including WSL)
  PORT ?= /dev/ttyUSB0
endif

# usbipd bus ID for WSL USB passthrough.  Find yours with:
#   powershell> usbipd list
# The bind step (one-time, see README) must be done before first use.
USBIPD_BUSID ?= 1-2

# Verbose mode — set VERBOSE=1 to see detailed compilation output
VERBOSE ?= 0
VERBOSE_FLAG = $(if $(filter 1,$(VERBOSE)),--verbose,)

# Define lists — values come from node_config.mk; $(if) skips any that are unset
# (the C headers' #ifndef defaults take over for missing values).
STRING_DEFINES  = DEFAULT_NODE_ID LED_ORDER
NUMERIC_DEFINES = LED_BRIGHTNESS DEBUG CMD_DEBUG UPDATE_CFG NODE_VERSION

# Build -D flags. $(strip) handles trailing whitespace from inline comments.
STRING_DEFS  = $(foreach def,$(STRING_DEFINES),$(if $($(def)),-D$(def)=\"$(strip $($(def)))\"))
NUMERIC_DEFS = $(foreach def,$(NUMERIC_DEFINES),$(if $($(def)),-D$(def)=$(strip $($(def)))))

# Optional config overrides — only added when set (in node_config.mk or CLI).
ifdef TX_OUTPUT_POWER
  NUMERIC_DEFS += -DTX_OUTPUT_POWER=$(strip $(TX_OUTPUT_POWER))
endif
ifdef RX_DUTY_PERCENT_DEFAULT
  NUMERIC_DEFS += -DRX_DUTY_PERCENT_DEFAULT=$(strip $(RX_DUTY_PERCENT_DEFAULT))
endif
# Convert SENSORS list (from node_config.mk) to -DSENSOR_XXX=1 flags
SENSOR_DEFS = $(foreach s,$(SENSORS),-DSENSOR_$(shell echo $(s) | tr a-z A-Z)=1)

# Per-sensor rate defaults (only passed when set)
ifdef BME280_RATE_SEC_DEFAULT
  SENSOR_DEFS += -DBME280_RATE_SEC_DEFAULT=$(strip $(BME280_RATE_SEC_DEFAULT))
endif
ifdef BATT_RATE_SEC_DEFAULT
  SENSOR_DEFS += -DBATT_RATE_SEC_DEFAULT=$(strip $(BATT_RATE_SEC_DEFAULT))
endif

# WRITE_NODE_ID: one-time write of node ID to EEPROM.
# Usage: make upload WRITE_NODE_ID=ab02
ifdef WRITE_NODE_ID
  STRING_DEFS += -DWRITE_NODE_ID=\"$(WRITE_NODE_ID)\"
endif

ALL_DEFS = $(STRING_DEFS) $(NUMERIC_DEFS) $(SENSOR_DEFS)

# Include path for shared headers (packets.h, etc.)
INCLUDE_FLAGS = -I$(CURDIR)/shared

# Combine into single build properties (one for C, one for C++)
DEFINE_FLAGS = --build-property "compiler.c.extra_flags=$(ALL_DEFS) $(INCLUDE_FLAGS)" \
               --build-property "compiler.cpp.extra_flags=$(ALL_DEFS) $(INCLUDE_FLAGS)"

FQBN_FULL = $(FQBN):LORAWAN_REGION=$(strip $(LORAWAN_REGION)),LORAWAN_RGB=0

.PHONY: all compile upload monitor ensure-usb clean clean-all test

all: compile

compile:
	arduino-cli compile \
		--fqbn "$(FQBN_FULL)" \
		--build-path "$(BUILD_DIR)" \
		$(VERBOSE_FLAG) \
		$(DEFINE_FLAGS) \
		"data_log/data_log.ino"

ensure-usb:
ifeq ($(UNAME_S),Darwin)
	@# macOS: USB devices work directly — just check if port exists
	@if [ ! -c "$(PORT)" ]; then \
	  echo "ERROR: Serial port not found."; \
	  echo ""; \
	  echo "Available USB serial ports:"; \
	  ls /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART /dev/cu.wchusbserial* 2>/dev/null || echo "  (none found)"; \
	  echo ""; \
	  echo "Make sure your device is plugged in, then run:"; \
	  echo "  make upload PORT=/dev/cu.usbserial-XXXX"; \
	  exit 1; \
	fi
else
	@# Linux/WSL: attempt usbipd attach if port not found
	@if [ ! -c "$(PORT)" ]; then \
	  echo "$(PORT) not found — running usbipd attach (busid $(USBIPD_BUSID))..."; \
	  powershell.exe -Command "usbipd attach --wsl --busid $(USBIPD_BUSID)" || true; \
	  echo "Waiting for device to appear..."; \
	  for i in 1 2 3 4 5; do \
	    sleep 1; \
	    [ -c "$(PORT)" ] && break; \
	  done; \
	  if [ ! -c "$(PORT)" ]; then \
	    echo "ERROR: $(PORT) still not available."; \
	    echo "  Check that the device is plugged in and bound:"; \
	    echo "    powershell> usbipd list"; \
	    echo "    powershell> usbipd bind --busid $(USBIPD_BUSID) --force"; \
	    exit 1; \
	  fi; \
	fi
endif

upload: compile ensure-usb
	arduino-cli upload \
		--fqbn "$(FQBN_FULL)" \
		--port "$(PORT)" \
		--build-path "$(BUILD_DIR)" \
		$(VERBOSE_FLAG) \
		"data_log/data_log.ino"

monitor: ensure-usb
	arduino-cli monitor \
		--port "$(PORT)" \
		--config baudrate=115200

clean:
	rm -rf "$(BUILD_DIR)"

clean-all:
	rm -rf build
	$(MAKE) -C tests clean

test:
	$(MAKE) -C tests
