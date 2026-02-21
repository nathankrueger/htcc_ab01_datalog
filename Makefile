# Arduino CLI Makefile for CubeCell HTCC-AB01 (CubeCell-Board-V2)
#
# Usage:
#   make                           # compile default sketch (data_log)
#   make upload                    # compile + upload  (auto-attaches USB on WSL)
#   make SKETCH=range_test         # compile the range test sketch
#   make upload SKETCH=range_test  # compile + upload range test
#   make monitor                   # open serial monitor
#   make clean                     # remove build artifacts for current sketch
#   make clean-all                 # remove all build artifacts
#
# Override defaults on the command line, e.g.:
#   make LORAWAN_REGION=6                # switch to EU868
#   make upload PORT=/dev/ttyUSB1        # use a different serial port
#   make upload USBIPD_BUSID=2-3         # use a different USB bus ID (WSL only)
#   make VERBOSE=1                       # show detailed compilation output

SKETCH    ?= data_log
FQBN       = CubeCell:CubeCell:CubeCell-Board-V2
BUILD_DIR  = build/$(SKETCH)

# Locate sketch: check top-level first, then examples/
ifneq ($(wildcard $(SKETCH)/$(SKETCH).ino),)
  SKETCH_PATH = $(SKETCH)
else
  SKETCH_PATH = examples/$(SKETCH)
endif

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

# LoRaWAN region — default matches the 915 MHz frequency in the sketch.
#   0  AS923 (AS1)    5  EU433
#   1  AS923 (AS2)    6  EU868
#   2  AU915          7  KR920
#   3  CN470          8  IN865
#   4  CN779          9  US915          <-- default
#                    10  US915 HYBRID
LORAWAN_REGION ?= 9

# Parameters for this instance — passed as compiler defines.
NODE_ID ?= ab01
LED_BRIGHTNESS ?= 16
DEBUG ?= 0
LED_ORDER ?= GRB
UPDATE_CFG ?= 0
NODE_VERSION ?= 1

# String defines (will be quoted)
STRING_DEFINES = NODE_ID LED_ORDER
# Numeric defines (no quotes)
GPS_LED ?= 0
CMD_DEBUG ?= 0
NUMERIC_DEFINES = LED_BRIGHTNESS DEBUG GPS_LED CMD_DEBUG UPDATE_CFG NODE_VERSION

# Build the combined define strings for C and C++ flags
# String defines get quotes, numeric defines don't
STRING_DEFS = $(foreach def,$(STRING_DEFINES),-D$(def)=\"$($(def))\")
NUMERIC_DEFS = $(foreach def,$(NUMERIC_DEFINES),-D$(def)=$($(def)))

# Optional config overrides — only passed to compiler when set on command line.
# e.g.:  make upload TX_OUTPUT_POWER=20 RX_DUTY_PERCENT_DEFAULT=50
ifdef TX_OUTPUT_POWER
  NUMERIC_DEFS += -DTX_OUTPUT_POWER=$(TX_OUTPUT_POWER)
endif
ifdef RX_DUTY_PERCENT_DEFAULT
  NUMERIC_DEFS += -DRX_DUTY_PERCENT_DEFAULT=$(RX_DUTY_PERCENT_DEFAULT)
endif
ifdef SENSOR_RATE_SEC_DEFAULT
  NUMERIC_DEFS += -DSENSOR_RATE_SEC_DEFAULT=$(SENSOR_RATE_SEC_DEFAULT)
endif

# WRITE_NODE_ID: one-time write of node ID to EEPROM.
# Usage: make upload WRITE_NODE_ID=ab02
ifdef WRITE_NODE_ID
  STRING_DEFS += -DWRITE_NODE_ID=\"$(WRITE_NODE_ID)\"
  NODE_ID := $(WRITE_NODE_ID)
endif

# Board-V2 lacks GPIO8; define a placeholder so the DISPLAY library's
# ST7735 driver compiles (range_test uses SSD1306 only, ST7735 is never called).
ifeq ($(SKETCH),range_test)
  NUMERIC_DEFS += -DGPIO8=GPIO0
endif

ALL_DEFS = $(STRING_DEFS) $(NUMERIC_DEFS)

# Include path for shared headers (packets.h, etc.)
INCLUDE_FLAGS = -I$(CURDIR)/shared

# Combine into single build properties (one for C, one for C++)
DEFINE_FLAGS = --build-property "compiler.c.extra_flags=$(ALL_DEFS) $(INCLUDE_FLAGS)" \
               --build-property "compiler.cpp.extra_flags=$(ALL_DEFS) $(INCLUDE_FLAGS)"

FQBN_FULL = $(FQBN):LORAWAN_REGION=$(LORAWAN_REGION),LORAWAN_RGB=0

.PHONY: all compile upload monitor ensure-usb clean clean-all test

all: compile

compile:
	arduino-cli compile \
		--fqbn "$(FQBN_FULL)" \
		--build-path "$(BUILD_DIR)" \
		$(VERBOSE_FLAG) \
		$(DEFINE_FLAGS) \
		"$(SKETCH_PATH)/$(SKETCH).ino"

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
		"$(SKETCH_PATH)/$(SKETCH).ino"

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
