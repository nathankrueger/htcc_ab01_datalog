/*
 * config_types.h — EEPROM struct definitions
 *
 * Separated from config.h so it can be included by unit tests and shared
 * headers without pulling in Arduino-specific EEPROM code.
 *
 * EEPROM layout:
 *   Byte 0:      NODE_ID_MAGIC (0x4E)  — "has node ID been written?"
 *   Bytes 1-16:  nodeId[16]            — unversioned, permanent
 *   Byte 17:     CFG_MAGIC (0xCF)      — "has config been written?"
 *   Byte 18:     cfgVersion (4)        — "is the layout current?"
 *   Bytes 19+:   config fields         — versioned, can grow
 */

#ifndef CONFIG_TYPES_H
#define CONFIG_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* ─── Node Identity (bytes 0-16, unversioned) ────────────────────────────── */

#define NODE_ID_MAGIC 0x4E        /* Sentinel — "has node ID been written?" */

typedef struct __attribute__((packed)) {
    uint8_t magic;               /*  1B — NODE_ID_MAGIC when written        */
    char    nodeId[16];          /* 16B — null-terminated identifier        */
} NodeIdentity;                  /* 17B at offset 0                         */

#define NODEID_REGION_SIZE  sizeof(NodeIdentity)   /* 17 */
#define CFG_EEPROM_OFFSET   NODEID_REGION_SIZE     /* NodeConfig starts here */

/* ─── Versioned Config (bytes 17+, resets on CFG_VERSION bump) ───────────── */

#define CFG_MAGIC       0xCF      /* Sentinel — "has config been written?"  */
#define CFG_VERSION     2         /* Bump when NodeConfig fields change     */

typedef struct __attribute__((packed)) NodeConfig {
    uint8_t  magic;              /*  1B — CFG_MAGIC when written            */
    uint8_t  cfgVersion;         /*  1B — struct layout version             */
    int8_t   txOutputPower;      /*  1B — TX power in dBm                  */
    uint8_t  rxDutyPercent;      /*  1B — RX duty cycle 0-100              */
    uint8_t  spreadingFactor;    /*  1B — SF7-SF12                          */
    uint8_t  bandwidth;          /*  1B — 0=125kHz, 1=250kHz, 2=500kHz     */
    uint32_t n2gFrequencyHz;     /*  4B — Node-to-Gateway freq (Hz)         */
    uint32_t g2nFrequencyHz;     /*  4B — Gateway-to-Node freq (Hz)         */
    uint16_t sensorRateSec;      /*  2B — seconds between sensor TX         */
    uint16_t broadcastAckJitterMs; /* 2B — Max jitter before ACK (0=off)    */
} NodeConfig;                    /* 18B at offset 17                        */

#endif /* CONFIG_TYPES_H */
