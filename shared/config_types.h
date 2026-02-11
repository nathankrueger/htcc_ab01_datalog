/*
 * config_types.h — NodeConfig struct definition
 *
 * Separated from config.h so it can be included by unit tests and shared
 * headers without pulling in Arduino-specific EEPROM code.
 */

#ifndef CONFIG_TYPES_H
#define CONFIG_TYPES_H

#include <stdint.h>

#define CFG_MAGIC   0xCF          /* Fixed sentinel — never changes     */
#define CFG_VERSION 2             /* Bump when NodeConfig fields change */

typedef struct __attribute__((packed)) NodeConfig {
    uint8_t  magic;              /*  1B — fixed sentinel (CFG_MAGIC)   */
    uint8_t  cfgVersion;         /*  1B — struct layout version        */
    char     nodeId[16];         /* 16B — null-terminated identifier   */
    int8_t   txOutputPower;      /*  1B — TX power in dBm             */
    uint8_t  rxDutyPercent;      /*  1B — RX duty cycle 0-100         */
    uint8_t  spreadingFactor;    /*  1B — SF7-SF12                     */
    uint8_t  bandwidth;          /*  1B — 0=125kHz, 1=250kHz, 2=500kHz */
} NodeConfig;                    /* 22B total                          */

#endif /* CONFIG_TYPES_H */
