# Arduino CLI Makefile for CubeCell HTCC-AB01 — Range Test Sketch
#
# Usage (from repo root):
#   make -C range_test                # compile
#   make -C range_test upload         # compile + upload
#   make -C range_test monitor        # serial monitor
#   make -C range_test clean          # remove build artifacts
#
# Override defaults on the command line, e.g.:
#   make -C range_test upload GPS_LED=1
#   make -C range_test upload PORT=/dev/ttyUSB1

FQBN       = CubeCell:CubeCell:CubeCell-Board-V2
BUILD_DIR  = ../build/range_test

# ─── Platform detection ──────────────────────────────────────────────────────
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  PORT ?= $(shell ls /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART /dev/cu.wchusbserial* 2>/dev/null | head -1)
  ifeq ($(PORT),)
    PORT = __no_device_found__
  endif
else
  PORT ?= /dev/ttyUSB0
endif

USBIPD_BUSID ?= 1-2

VERBOSE ?= 0
VERBOSE_FLAG = $(if $(filter 1,$(VERBOSE)),--verbose,)

LORAWAN_REGION ?= 9

# ─── Build Parameters ────────────────────────────────────────────────────────
NODE_ID ?= range_test
LED_BRIGHTNESS ?= 16
LED_ORDER ?= GRB
DEBUG ?= 1
GPS_LED ?= 0

# String defines (will be quoted)
STRING_DEFS = -DNODE_ID=\"$(NODE_ID)\" -DLED_ORDER=\"$(LED_ORDER)\"

# Numeric defines
NUMERIC_DEFS = -DLED_BRIGHTNESS=$(LED_BRIGHTNESS) -DDEBUG=$(DEBUG) -DGPS_LED=$(GPS_LED)

# Board-V2 lacks GPIO8; define a placeholder so the DISPLAY library's
# ST7735 driver compiles (we use SSD1306 only, ST7735 is never called).
NUMERIC_DEFS += -DGPIO8=GPIO0

ALL_DEFS = $(STRING_DEFS) $(NUMERIC_DEFS)

# Include path for shared headers (packets.h, radio.h, etc.)
INCLUDE_FLAGS = -I$(CURDIR)/../shared

DEFINE_FLAGS = --build-property "compiler.c.extra_flags=$(ALL_DEFS) $(INCLUDE_FLAGS)" \
               --build-property "compiler.cpp.extra_flags=$(ALL_DEFS) $(INCLUDE_FLAGS)"

FQBN_FULL = $(FQBN):LORAWAN_REGION=$(LORAWAN_REGION),LORAWAN_RGB=0

.PHONY: all compile upload monitor ensure-usb clean

all: compile

compile:
	arduino-cli compile \
		--fqbn "$(FQBN_FULL)" \
		--build-path "$(BUILD_DIR)" \
		$(VERBOSE_FLAG) \
		$(DEFINE_FLAGS) \
		"range_test.ino"

ensure-usb:
ifeq ($(UNAME_S),Darwin)
	@if [ ! -c "$(PORT)" ]; then \
	  echo "ERROR: Serial port not found."; \
	  echo ""; \
	  echo "Available USB serial ports:"; \
	  ls /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART /dev/cu.wchusbserial* 2>/dev/null || echo "  (none found)"; \
	  echo ""; \
	  echo "Make sure your device is plugged in, then run:"; \
	  echo "  make -C range_test upload PORT=/dev/cu.usbserial-XXXX"; \
	  exit 1; \
	fi
else
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
		"range_test.ino"

monitor: ensure-usb
	arduino-cli monitor \
		--port "$(PORT)" \
		--config baudrate=115200

clean:
	rm -rf "$(BUILD_DIR)"
