# Arduino CLI Makefile for CubeCell HTCC-AB01 (CubeCell-Board-V2)
#
# Usage:
#   make            # compile
#   make upload     # compile + upload  (auto-attaches USB on WSL)
#   make monitor    # open serial monitor
#   make clean      # remove build artifacts
#
# Override defaults on the command line, e.g.:
#   make LORAWAN_REGION=6                # switch to EU868
#   make upload PORT=/dev/ttyUSB1        # use a different serial port
#   make upload USBIPD_BUSID=2-3         # use a different USB bus ID

SKETCH    = htcc_ab01_datalog.ino
FQBN      = CubeCell:CubeCell:CubeCell-Board-V2
PORT     ?= /dev/ttyUSB0
BUILD_DIR = build

# usbipd bus ID for WSL USB passthrough.  Find yours with:
#   powershell> usbipd list
# The bind step (one-time, see README) must be done before first use.
USBIPD_BUSID ?= 1-2

# LoRaWAN region — default matches the 915 MHz frequency in the sketch.
#   0  AS923 (AS1)    5  EU433
#   1  AS923 (AS2)    6  EU868
#   2  AU915          7  KR920
#   3  CN470          8  IN865
#   4  CN779          9  US915          <-- default
#                    10  US915 HYBRID
LORAWAN_REGION ?= 9

FQBN_FULL = $(FQBN):LORAWAN_REGION=$(LORAWAN_REGION)

.PHONY: all compile upload monitor clean

all: compile

compile:
	arduino-cli compile \
		--fqbn "$(FQBN_FULL)" \
		--build-path "$(BUILD_DIR)" \
		"$(SKETCH)"

upload: compile
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
	arduino-cli upload \
		--fqbn "$(FQBN_FULL)" \
		--port "$(PORT)" \
		--build-path "$(BUILD_DIR)" \
		"$(SKETCH)"

monitor:
	arduino-cli monitor \
		--port "$(PORT)" \
		--config baudrate=115200

clean:
	rm -rf "$(BUILD_DIR)"
