/*
 * config.h — EEPROM-backed persistent configuration for CubeCell HTCC-AB01
 *
 * Stores node identity and tunable parameters in EEPROM (768 bytes available).
 * Values survive power cycles and resets.
 *
 * Workflow:
 *   1. Compile-time #defines provide defaults (overridable via Makefile -D flags)
 *   2. cfgLoad() reads EEPROM into a NodeConfig struct
 *   3. If EEPROM is uninitialised (no magic byte), compile-time defaults are used
 *   4. When UPDATE_CFG=1, compile-time values replace EEPROM contents
 *      (EEPROM is only written if the values actually differ — no unnecessary wear)
 *
 * Requires radio.h to be included first (for DEFAULT_TX_POWER).
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <EEPROM.h>

/* ─── Compile-Time Defaults (overridable via Makefile) ────────────────────── */

#ifndef NODE_ID
#define NODE_ID                  "ab01"
#endif

#ifndef NODE_VERSION
#define NODE_VERSION             1
#endif

#ifndef TX_OUTPUT_POWER
#define TX_OUTPUT_POWER          DEFAULT_TX_POWER   /* 14 dBm, from radio.h */
#endif

#ifndef RX_DUTY_PERCENT_DEFAULT
#define RX_DUTY_PERCENT_DEFAULT  90
#endif

#ifndef UPDATE_CFG
#define UPDATE_CFG               0
#endif

/* ─── Config Struct ───────────────────────────────────────────────────────── */

#define CFG_MAGIC 0xCF

struct __attribute__((packed)) NodeConfig {
    uint8_t  magic;              /*  1B — validity marker              */
    char     nodeId[16];         /* 16B — null-terminated identifier   */
    uint16_t nodeVersion;        /*  2B — firmware / config version    */
    int8_t   txOutputPower;      /*  1B — TX power in dBm             */
    uint8_t  rxDutyPercent;      /*  1B — RX duty cycle 0-100         */
};                               /* 21B total                          */

/* ─── Helpers ─────────────────────────────────────────────────────────────── */

/* Populate a NodeConfig from compile-time #defines. */
static inline void cfgDefaults(NodeConfig *c)
{
    c->magic = CFG_MAGIC;
    strncpy(c->nodeId, NODE_ID, sizeof(c->nodeId) - 1);
    c->nodeId[sizeof(c->nodeId) - 1] = '\0';
    c->nodeVersion    = NODE_VERSION;
    c->txOutputPower  = TX_OUTPUT_POWER;
    c->rxDutyPercent  = RX_DUTY_PERCENT_DEFAULT;
}

/*
 * Save *c to EEPROM.  Reads back the current EEPROM contents first and
 * only writes when they differ (explicit memcmp — we don't rely on the
 * library's internal comparison).
 *
 * Returns true if a flash write occurred.
 */
static inline bool cfgSave(NodeConfig *c)
{
    NodeConfig existing;
    EEPROM.get(0, existing);
    if (memcmp(&existing, c, sizeof(NodeConfig)) == 0)
        return false;               /* identical — skip write */

    EEPROM.put(0, *c);
    EEPROM.commit();
    return true;
}

/*
 * Load configuration from EEPROM into *c.
 *
 * Returns true if EEPROM contained a valid config, false if defaults were used.
 *
 * When UPDATE_CFG == 1 the struct is always populated from compile-time
 * #defines and written to EEPROM (only if the bytes actually differ).
 */
static inline bool cfgLoad(NodeConfig *c)
{
    EEPROM.begin(sizeof(NodeConfig));
    EEPROM.get(0, *c);

    bool valid = (c->magic == CFG_MAGIC);

    if (!valid) {
        /* First boot or blank EEPROM — use compile-time defaults.
         * Don't write to EEPROM; the user must opt in with UPDATE_CFG=1. */
        cfgDefaults(c);
    }

#if UPDATE_CFG
    cfgDefaults(c);
    cfgSave(c);
#endif

    return valid;
}

#endif /* CONFIG_H */
