/*
 * config.h — EEPROM-backed persistent configuration for CubeCell HTCC-AB01
 *
 * Stores node identity and tunable parameters in EEPROM (768 bytes available).
 * Values survive power cycles and resets.
 *
 * Workflow:
 *   1. Compile-time #defines provide defaults (overridable via Makefile -D flags)
 *   2. cfgLoad() reads EEPROM into a NodeConfig struct
 *   3. If EEPROM is uninitialised (wrong magic/version), compile-time defaults are used
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

#ifndef SPREADING_FACTOR_DEFAULT
#define SPREADING_FACTOR_DEFAULT LORA_SPREADING_FACTOR  /* SF7, from radio.h */
#endif

#ifndef BANDWIDTH_DEFAULT
#define BANDWIDTH_DEFAULT        LORA_BANDWIDTH  /* 0 = 125 kHz, from radio.h */
#endif

#ifndef N2G_FREQUENCY_DEFAULT
#define N2G_FREQUENCY_DEFAULT    RF_N2G_FREQUENCY  /* 915 MHz, from radio.h */
#endif

#ifndef G2N_FREQUENCY_DEFAULT
#define G2N_FREQUENCY_DEFAULT    RF_G2N_FREQUENCY  /* 915.5 MHz, from radio.h */
#endif

#ifndef SENSOR_RATE_SEC_DEFAULT
#define SENSOR_RATE_SEC_DEFAULT  5                  /* seconds between sensor TX */
#endif

#ifndef UPDATE_CFG
#define UPDATE_CFG               0
#endif

/* ─── Config Struct (shared with unit tests via config_types.h) ───────────── */

#include "config_types.h"

/* ─── Helpers ─────────────────────────────────────────────────────────────── */

/* Populate a NodeConfig from compile-time #defines. */
static inline void cfgDefaults(NodeConfig *c)
{
    c->magic      = CFG_MAGIC;
    c->cfgVersion = CFG_VERSION;
    strncpy(c->nodeId, NODE_ID, sizeof(c->nodeId) - 1);
    c->nodeId[sizeof(c->nodeId) - 1] = '\0';
    c->txOutputPower   = TX_OUTPUT_POWER;
    c->rxDutyPercent   = RX_DUTY_PERCENT_DEFAULT;
    c->spreadingFactor = SPREADING_FACTOR_DEFAULT;
    c->bandwidth       = BANDWIDTH_DEFAULT;
    c->n2gFrequencyHz  = N2G_FREQUENCY_DEFAULT;
    c->g2nFrequencyHz  = G2N_FREQUENCY_DEFAULT;
    c->sensorRateSec   = SENSOR_RATE_SEC_DEFAULT;
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

    bool valid = (c->magic == CFG_MAGIC && c->cfgVersion == CFG_VERSION);

    if (!valid) {
        /* First boot, blank EEPROM, or struct layout changed — use defaults.
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
