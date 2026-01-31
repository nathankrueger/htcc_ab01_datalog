# Arduino CLI Makefile for CubeCell HTCC-AB01 (CubeCell-Board-V2)
#
# Usage:
#   make            # compile
#   make upload     # compile + upload
#   make monitor    # open serial monitor
#   make clean      # remove build artifacts
#
# Override defaults on the command line, e.g.:
#   make LORAWAN_REGION=6          # switch to EU868
#   make upload PORT=/dev/ttyUSB1  # use a different serial port

SKETCH    = htcc_ab01_datalog.ino
FQBN      = CubeCell:CubeCell:CubeCell-Board-V2
PORT     ?= /dev/ttyUSB0
BUILD_DIR = build

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
