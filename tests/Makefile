CC       = gcc
CFLAGS   = -Wall -Wextra -Wpedantic -std=c11 -I../shared -I../data_log -g
SRCS     = test_main.c
BUILD_DIR = ../build/tests
TARGET    = $(BUILD_DIR)/test_runner

.PHONY: all clean

all: $(TARGET)
	$(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(SRCS) test_params.c test_sensors.c test_harness.h ../shared/params.h ../shared/packets.h ../shared/config_types.h ../data_log/sensor_drv.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -rf $(BUILD_DIR)
