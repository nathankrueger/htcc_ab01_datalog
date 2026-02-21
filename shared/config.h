/*
 * config.h — EEPROM-backed persistent configuration for CubeCell HTCC-AB01
 *
 * EEPROM layout (768 bytes available):
 *   Bytes 0-16:  NodeIdentity — unversioned node ID (survives CFG_VERSION bumps)
 *   Bytes 17+:   NodeConfig   — versioned tunable params (resets on version change)
 *
 * Workflow:
 *   1. Compile-time #defines provide defaults (overridable via Makefile -D flags)
 *   2. cfgLoad() reads NodeConfig from EEPROM offset 17
 *   3. If cfgVersion doesn't match, compile-time defaults are used
 *   4. cfgLoadNodeId() reads node ID from EEPROM offset 0 (independent of config)
 *   5. WRITE_NODE_ID compile flag writes node ID to EEPROM on boot (one-time)
 *   6. UPDATE_CFG=1 forces config to compile-time defaults (does not touch node ID)
 *
 * Requires radio.h to be included first (for DEFAULT_TX_POWER).
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <EEPROM.h>

/* ─── Compile-Time Defaults (overridable via Makefile) ────────────────────── */

#ifndef DEFAULT_NODE_ID
#define DEFAULT_NODE_ID          "empty"
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

#ifndef BME280_RATE_SEC_DEFAULT
#define BME280_RATE_SEC_DEFAULT  30                 /* BME280 sample interval (s) */
#endif

#ifndef BATT_RATE_SEC_DEFAULT
#define BATT_RATE_SEC_DEFAULT    60                 /* Battery sample interval (s) */
#endif

#ifndef BROADCAST_ACK_JITTER_DEFAULT
#define BROADCAST_ACK_JITTER_DEFAULT 1000           /* ms, 0 to disable */
#endif

#ifndef UPDATE_CFG
#define UPDATE_CFG               0
#endif

/* ─── Config Struct (shared with unit tests via config_types.h) ───────────── */

#include "config_types.h"
#include "dbg.h"

/* ─── Node Identity (EEPROM offset 0, unversioned) ────────────────────────── */

/*
 * Load node ID from the unversioned EEPROM region (offset 0).
 * If NODE_ID_MAGIC is missing (blank/uninitialised), copies compile-time
 * DEFAULT_NODE_ID as fallback.  buf must be at least 16 bytes.
 *
 * Call AFTER cfgLoad() (which initialises EEPROM).
 */
static inline void cfgLoadNodeId(char *buf)
{
    NodeIdentity nid;
    EEPROM.get(0, nid);

    if (nid.magic == NODE_ID_MAGIC && nid.nodeId[0] != '\0') {
        memcpy(buf, nid.nodeId, sizeof(nid.nodeId));
        buf[sizeof(nid.nodeId) - 1] = '\0';
        DBG("[CFG] loadNodeId: EEPROM \"%s\"\n", buf);
    } else {
        strncpy(buf, DEFAULT_NODE_ID, 15);
        buf[15] = '\0';
        DBG("[CFG] loadNodeId: magic mismatch (0x%02X != 0x%02X), using default \"%s\"\n",
            nid.magic, NODE_ID_MAGIC, buf);
    }
}

/*
 * Write node ID to the unversioned EEPROM region (offset 0).
 * Only writes if the data actually differs (wear leveling).
 * Returns true if a flash write occurred.
 */
static inline bool cfgSaveNodeId(const char *id)
{
    NodeIdentity nid;
    nid.magic = NODE_ID_MAGIC;
    memset(nid.nodeId, 0, sizeof(nid.nodeId));
    strncpy(nid.nodeId, id, sizeof(nid.nodeId) - 1);

    NodeIdentity existing;
    EEPROM.get(0, existing);
    if (memcmp(&existing, &nid, sizeof(NodeIdentity)) == 0) {
        DBG("[CFG] saveNodeId: \"%s\" unchanged, skip write\n", id);
        return false;
    }

    EEPROM.put(0, nid);
    EEPROM.commit();
    DBG("[CFG] saveNodeId: wrote \"%s\" to EEPROM\n", id);
    return true;
}

/* ─── Versioned Config (EEPROM offset 17) ─────────────────────────────────── */

/* Populate a NodeConfig from compile-time #defines. */
static inline void cfgDefaults(NodeConfig *c)
{
    c->magic           = CFG_MAGIC;
    c->cfgVersion      = CFG_VERSION;
    c->txOutputPower   = TX_OUTPUT_POWER;
    c->rxDutyPercent   = RX_DUTY_PERCENT_DEFAULT;
    c->spreadingFactor = SPREADING_FACTOR_DEFAULT;
    c->bandwidth       = BANDWIDTH_DEFAULT;
    c->n2gFrequencyHz  = N2G_FREQUENCY_DEFAULT;
    c->g2nFrequencyHz  = G2N_FREQUENCY_DEFAULT;
    c->broadcastAckJitterMs = BROADCAST_ACK_JITTER_DEFAULT;
    c->bme280RateSec   = BME280_RATE_SEC_DEFAULT;
    c->battRateSec     = BATT_RATE_SEC_DEFAULT;
}

/*
 * Save *c to EEPROM at CFG_EEPROM_OFFSET.  Reads back the current contents
 * first and only writes when they differ (no unnecessary wear).
 *
 * Returns true if a flash write occurred.
 */
static inline bool cfgSave(NodeConfig *c)
{
    NodeConfig existing;
    EEPROM.get(CFG_EEPROM_OFFSET, existing);
    if (memcmp(&existing, c, sizeof(NodeConfig)) == 0) {
        DBG("[CFG] save: unchanged, skip write\n");
        return false;
    }

    EEPROM.put(CFG_EEPROM_OFFSET, *c);
    EEPROM.commit();
    DBG("[CFG] save: wrote %u bytes to EEPROM offset %u\n",
        (unsigned)sizeof(NodeConfig), (unsigned)CFG_EEPROM_OFFSET);
    return true;
}

/*
 * Load configuration from EEPROM into *c.
 *
 * Initialises the EEPROM subsystem (covers both NodeIdentity and NodeConfig
 * regions).  Returns true if EEPROM contained a valid config, false if
 * defaults were used.
 *
 * When UPDATE_CFG == 1 the struct is always populated from compile-time
 * #defines and written to EEPROM (only if the bytes actually differ).
 */
static inline bool cfgLoad(NodeConfig *c)
{
    EEPROM.begin(CFG_EEPROM_OFFSET + sizeof(NodeConfig));
    EEPROM.get(CFG_EEPROM_OFFSET, *c);

    bool valid = (c->magic == CFG_MAGIC && c->cfgVersion == CFG_VERSION);

    if (!valid) {
        /* First boot, blank EEPROM, or struct layout changed — use defaults.
         * Don't write to EEPROM; the user must opt in with UPDATE_CFG=1. */
        if (c->magic != CFG_MAGIC)
            DBG("[CFG] load: magic mismatch (0x%02X != 0x%02X)\n",
                c->magic, CFG_MAGIC);
        if (c->cfgVersion != CFG_VERSION)
            DBG("[CFG] load: version mismatch (%u != %u)\n",
                c->cfgVersion, CFG_VERSION);

        /* Reset to defaults, invalid content in EEPROM */
        DBG("[CFG] load: using compile-time defaults\n");
        cfgDefaults(c);
    } else {
        /* Content loaded from EEPROM valid */
        DBG("[CFG] load: valid config from EEPROM (v%u)\n", c->cfgVersion);
    }

#if UPDATE_CFG
    DBG("[CFG] load: UPDATE_CFG=1, forcing defaults\n");
    cfgDefaults(c);
    cfgSave(c);
#endif

    return valid;
}

#endif /* CONFIG_H */
