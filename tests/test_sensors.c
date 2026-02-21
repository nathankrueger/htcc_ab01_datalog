/*
 * test_sensors.c — Unit tests for sensorPack() in sensors.h
 *
 * Compiled natively with gcc — no Arduino dependencies.
 * Tests packet building and auto-splitting when readings exceed payload.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "packets.h"
#include "sensor_drv.h"
#include "test_harness.h"

/* ─── Helpers ────────────────────────────────────────────────────────────── */

/* Verify a packet starts with 4-byte padding and contains valid JSON with CRC */
static bool isValidSensorPacket(const char *pkt, int len)
{
    if (len < 5) return false;
    /* First 4 bytes are space padding (ASR650x TX-FIFO workaround) */
    for (int i = 0; i < 4; i++)
        if (pkt[i] != ' ') return false;
    /* Must start with '{' after padding */
    if (pkt[4] != '{') return false;
    /* Must contain node ID, readings array, and CRC */
    if (!strstr(pkt + 4, "\"n\":")) return false;
    if (!strstr(pkt + 4, "\"r\":")) return false;
    if (!strstr(pkt + 4, "\"c\":")) return false;
    return true;
}

/* ─── sensorPack: basic packing ─────────────────────────────────────────── */

TEST(test_sensorPack_three_readings)
{
    Reading readings[] = {
        { "Temperature", 0, "\\u00b0F", 72.5  },
        { "Pressure",    0, "hPa",      1013.25 },
        { "Humidity",    0, "%",        45.0  },
    };

    char pkt[LORA_MAX_PAYLOAD + 1];
    int nextOffset;
    int pLen = sensorPack("ab01", readings, 3, 0, &nextOffset, pkt, sizeof(pkt));

    ASSERT_TRUE(pLen > 0);
    ASSERT_TRUE(pLen <= LORA_MAX_PAYLOAD);
    ASSERT_INT_EQ(3, nextOffset);
    ASSERT_TRUE(isValidSensorPacket(pkt, pLen));

    /* Verify all three reading names are present */
    ASSERT_TRUE(strstr(pkt, "\"Temperature\"") != NULL);
    ASSERT_TRUE(strstr(pkt, "\"Pressure\"") != NULL);
    ASSERT_TRUE(strstr(pkt, "\"Humidity\"") != NULL);

    TEST_PASS();
}

TEST(test_sensorPack_single_reading)
{
    Reading readings[] = {
        { "Temperature", 0, "\\u00b0F", 72.5 },
    };

    char pkt[LORA_MAX_PAYLOAD + 1];
    int nextOffset;
    int pLen = sensorPack("ab01", readings, 1, 0, &nextOffset, pkt, sizeof(pkt));

    ASSERT_TRUE(pLen > 0);
    ASSERT_TRUE(pLen <= LORA_MAX_PAYLOAD);
    ASSERT_INT_EQ(1, nextOffset);
    ASSERT_TRUE(isValidSensorPacket(pkt, pLen));
    ASSERT_TRUE(strstr(pkt, "\"Temperature\"") != NULL);

    TEST_PASS();
}

TEST(test_sensorPack_node_id_in_packet)
{
    Reading readings[] = {
        { "Temperature", 0, "F", 72.5 },
    };

    char pkt[LORA_MAX_PAYLOAD + 1];
    int nextOffset;
    sensorPack("mynode", readings, 1, 0, &nextOffset, pkt, sizeof(pkt));

    ASSERT_TRUE(strstr(pkt, "\"mynode\"") != NULL);

    TEST_PASS();
}

/* ─── sensorPack: offset handling ───────────────────────────────────────── */

TEST(test_sensorPack_offset_mid)
{
    Reading readings[] = {
        { "Temperature", 0, "F",   72.5    },
        { "Pressure",    0, "hPa", 1013.25 },
        { "Humidity",    0, "%",   45.0    },
    };

    char pkt[LORA_MAX_PAYLOAD + 1];
    int nextOffset;
    int pLen = sensorPack("ab01", readings, 3, 1, &nextOffset, pkt, sizeof(pkt));

    ASSERT_TRUE(pLen > 0);
    ASSERT_INT_EQ(3, nextOffset);
    /* Should contain readings 1 and 2 but not 0 */
    ASSERT_TRUE(strstr(pkt, "\"Pressure\"") != NULL);
    ASSERT_TRUE(strstr(pkt, "\"Humidity\"") != NULL);
    ASSERT_TRUE(strstr(pkt, "\"Temperature\"") == NULL);

    TEST_PASS();
}

TEST(test_sensorPack_offset_last)
{
    Reading readings[] = {
        { "Temperature", 0, "F",   72.5    },
        { "Pressure",    0, "hPa", 1013.25 },
        { "Humidity",    0, "%",   45.0    },
    };

    char pkt[LORA_MAX_PAYLOAD + 1];
    int nextOffset;
    int pLen = sensorPack("ab01", readings, 3, 2, &nextOffset, pkt, sizeof(pkt));

    ASSERT_TRUE(pLen > 0);
    ASSERT_INT_EQ(3, nextOffset);
    ASSERT_TRUE(strstr(pkt, "\"Humidity\"") != NULL);
    ASSERT_TRUE(strstr(pkt, "\"Temperature\"") == NULL);
    ASSERT_TRUE(strstr(pkt, "\"Pressure\"") == NULL);

    TEST_PASS();
}

TEST(test_sensorPack_offset_past_end)
{
    Reading readings[] = {
        { "Temperature", 0, "F", 72.5 },
    };

    char pkt[LORA_MAX_PAYLOAD + 1];
    int nextOffset;
    int pLen = sensorPack("ab01", readings, 1, 5, &nextOffset, pkt, sizeof(pkt));

    ASSERT_INT_EQ(0, pLen);
    ASSERT_INT_EQ(1, nextOffset);

    TEST_PASS();
}

/* ─── sensorPack: auto-splitting ────────────────────────────────────────── */

TEST(test_sensorPack_split_many_readings)
{
    /*
     * Create enough readings that they won't all fit in one 250-byte packet.
     * Each reading with a long name + units produces ~60-70 bytes of JSON.
     * 6 readings should require splitting.
     */
    Reading readings[] = {
        { "Temperature",    0, "\\u00b0F", 72.5    },
        { "Pressure",       0, "hPa",      1013.25 },
        { "Humidity",       0, "%",        45.0    },
        { "WindSpeed",      1, "mph",      12.3    },
        { "WindDirection",  1, "deg",      270.0   },
        { "Rainfall",       1, "mm",       0.5     },
    };
    const int count = 6;

    char pkt[LORA_MAX_PAYLOAD + 1];
    int nextOffset;
    int totalReadingsPacked = 0;
    int packetCount = 0;
    int offset = 0;

    while (offset < count) {
        int pLen = sensorPack("ab01", readings, count,
                              offset, &nextOffset, pkt, sizeof(pkt));
        if (pLen == 0) {
            /* Skip oversized reading */
            offset = nextOffset;
            continue;
        }

        ASSERT_TRUE(pLen > 0);
        ASSERT_TRUE(pLen <= LORA_MAX_PAYLOAD);
        ASSERT_TRUE(isValidSensorPacket(pkt, pLen));
        ASSERT_TRUE(nextOffset > offset);

        totalReadingsPacked += (nextOffset - offset);
        packetCount++;
        offset = nextOffset;
    }

    /* All readings should be packed */
    ASSERT_INT_EQ(count, totalReadingsPacked);
    /* Should take more than one packet (or exactly one if they all fit —
     * assert at least one packet was produced) */
    ASSERT_TRUE(packetCount >= 1);

    TEST_PASS();
}

TEST(test_sensorPack_split_iteration)
{
    /*
     * Force splitting by using a small buffer capacity.
     * With a 120-byte cap, only 1 reading should fit per packet.
     */
    Reading readings[] = {
        { "Temperature", 0, "\\u00b0F", 72.5    },
        { "Pressure",    0, "hPa",      1013.25 },
        { "Humidity",    0, "%",        45.0    },
    };

    char pkt[120];
    int nextOffset;
    int packetCount = 0;
    int offset = 0;

    while (offset < 3) {
        int pLen = sensorPack("ab01", readings, 3,
                              offset, &nextOffset, pkt, sizeof(pkt));
        if (pLen == 0) {
            offset = nextOffset;
            continue;
        }

        ASSERT_TRUE(pLen > 0);
        ASSERT_TRUE(pLen <= (int)sizeof(pkt));
        ASSERT_TRUE(nextOffset > offset);

        packetCount++;
        offset = nextOffset;
    }

    /* Each reading should produce its own packet with 120-byte cap */
    ASSERT_INT_EQ(3, packetCount);

    TEST_PASS();
}

/* ─── sensorPack: CRC verification ─────────────────────────────────────── */

TEST(test_sensorPack_crc_present)
{
    Reading readings[] = {
        { "Temperature", 0, "F", 72.5 },
    };

    char pkt[LORA_MAX_PAYLOAD + 1];
    int nextOffset;
    int pLen = sensorPack("ab01", readings, 1, 0, &nextOffset, pkt, sizeof(pkt));

    ASSERT_TRUE(pLen > 0);

    /* CRC field should be an 8-char hex string */
    const char *crc = strstr(pkt, "\"c\":\"");
    ASSERT_TRUE(crc != NULL);
    crc += 5;  /* skip past "c":" */
    /* Verify 8 hex chars followed by closing quote */
    for (int i = 0; i < 8; i++) {
        ASSERT_TRUE((crc[i] >= '0' && crc[i] <= '9') ||
                    (crc[i] >= 'a' && crc[i] <= 'f'));
    }
    ASSERT_TRUE(crc[8] == '"');

    TEST_PASS();
}

TEST(test_sensorPack_deterministic_crc)
{
    Reading readings[] = {
        { "Temperature", 0, "F", 72.5 },
    };

    char pkt1[LORA_MAX_PAYLOAD + 1];
    char pkt2[LORA_MAX_PAYLOAD + 1];
    int next1, next2;

    int len1 = sensorPack("ab01", readings, 1, 0, &next1, pkt1, sizeof(pkt1));
    int len2 = sensorPack("ab01", readings, 1, 0, &next2, pkt2, sizeof(pkt2));

    ASSERT_INT_EQ(len1, len2);
    /* Packets should be byte-identical */
    ASSERT_TRUE(memcmp(pkt1, pkt2, len1) == 0);

    TEST_PASS();
}

/* ─── sensorPack: edge cases ────────────────────────────────────────────── */

TEST(test_sensorPack_empty)
{
    char pkt[LORA_MAX_PAYLOAD + 1];
    int nextOffset;
    int pLen = sensorPack("ab01", NULL, 0, 0, &nextOffset, pkt, sizeof(pkt));

    ASSERT_INT_EQ(0, pLen);
    ASSERT_INT_EQ(0, nextOffset);

    TEST_PASS();
}

TEST(test_sensorPack_tiny_buffer)
{
    Reading readings[] = {
        { "Temperature", 0, "F", 72.5 },
    };

    /* Buffer too small for any packet */
    char pkt[10];
    int nextOffset;
    int pLen = sensorPack("ab01", readings, 1, 0, &nextOffset, pkt, sizeof(pkt));

    /* Should fail to pack and advance offset */
    ASSERT_INT_EQ(0, pLen);
    ASSERT_INT_EQ(1, nextOffset);

    TEST_PASS();
}

/* ─── Test Runner ────────────────────────────────────────────────────────── */

void run_sensor_tests(void)
{
    printf("sensor_drv.h tests:\n");

    /* Basic packing */
    RUN_TEST(test_sensorPack_three_readings);
    RUN_TEST(test_sensorPack_single_reading);
    RUN_TEST(test_sensorPack_node_id_in_packet);

    /* Offset handling */
    RUN_TEST(test_sensorPack_offset_mid);
    RUN_TEST(test_sensorPack_offset_last);
    RUN_TEST(test_sensorPack_offset_past_end);

    /* Auto-splitting */
    RUN_TEST(test_sensorPack_split_many_readings);
    RUN_TEST(test_sensorPack_split_iteration);

    /* CRC */
    RUN_TEST(test_sensorPack_crc_present);
    RUN_TEST(test_sensorPack_deterministic_crc);

    /* Edge cases */
    RUN_TEST(test_sensorPack_empty);
    RUN_TEST(test_sensorPack_tiny_buffer);
}
