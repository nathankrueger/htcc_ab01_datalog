/*
 * params.h — Generic parameter registry with JSON response builders
 *
 * Provides a table-driven parameter system for get/set/list operations.
 * Each sketch defines its own param table (array of ParamDef) referencing
 * its runtime variables. This header provides the generic logic for:
 *
 *   - paramGet()           Single param lookup → JSON response
 *   - paramSet()           Validate + set + optional callback → JSON response
 *   - paramsList()         Paginated param listing → JSON with "more" flag
 *   - cmdsList()           Paginated command name listing → JSON with "more" flag
 *   - paramsSyncToConfig() Copy runtime params to NodeConfig for EEPROM persistence
 *
 * All JSON output uses alphabetically-sorted keys for CRC compatibility
 * with Python's json.dumps(sort_keys=True).
 *
 * No Arduino dependencies — compiles natively for unit tests.
 */

#ifndef PARAMS_H
#define PARAMS_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "config_types.h"

/* ─── Types ──────────────────────────────────────────────────────────────── */

typedef enum {
    PARAM_INT8 = 0,
    PARAM_UINT8,
    PARAM_INT16,
    PARAM_UINT16,
    PARAM_UINT32,
    PARAM_STRING
} ParamType;

/* Optional callback invoked after a param is updated via setparam. */
typedef void (*ParamOnSet)(const char *name);

/* Sentinel: param is not persisted to EEPROM. */
#define CFG_OFFSET_NONE 0xFF

typedef struct {
    const char  *name;       /* param name — table MUST be alpha-sorted by name */
    ParamType    type;
    void        *ptr;        /* setparam target: cfg field (staged) or runtime global (immediate) */
    void        *runtimePtr; /* staged params: runtime global to copy to on rcfg_radio; NULL = immediate */
    int16_t      minVal;     /* min allowed value (ignored for STRING) */
    int16_t      maxVal;     /* max allowed value (ignored for STRING) */
    bool         writable;   /* false = read-only via setparam */
    ParamOnSet   onSet;      /* optional callback after set (NULL if none) */
    uint8_t      cfgOffset;  /* offsetof() into NodeConfig, or CFG_OFFSET_NONE */
} ParamDef;

/* ─── Internal Helpers ───────────────────────────────────────────────────── */

/*
 * Format a single param as a JSON key:value fragment (no braces).
 * Returns bytes written (excluding null), or 0 if buffer too small.
 *   e.g. "txpwr":14   or   "nodeid":"ab01"
 */
static inline int paramFmtKV(const ParamDef *p, char *buf, int bufSize)
{
    /*
     * Use memcpy to read values via void* — avoids unaligned-access HardFault
     * on Cortex-M0+ when ptr points into a packed struct (e.g. NodeConfig).
     */
    int n = 0;
    switch (p->type) {
        case PARAM_INT8: {
            int8_t v; memcpy(&v, p->ptr, sizeof(v));
            n = snprintf(buf, bufSize, "\"%s\":%d", p->name, (int)v);
            break;
        }
        case PARAM_UINT8: {
            uint8_t v; memcpy(&v, p->ptr, sizeof(v));
            n = snprintf(buf, bufSize, "\"%s\":%u", p->name, (unsigned)v);
            break;
        }
        case PARAM_INT16: {
            int16_t v; memcpy(&v, p->ptr, sizeof(v));
            n = snprintf(buf, bufSize, "\"%s\":%d", p->name, (int)v);
            break;
        }
        case PARAM_UINT16: {
            uint16_t v; memcpy(&v, p->ptr, sizeof(v));
            n = snprintf(buf, bufSize, "\"%s\":%u", p->name, (unsigned)v);
            break;
        }
        case PARAM_UINT32: {
            uint32_t v; memcpy(&v, p->ptr, sizeof(v));
            n = snprintf(buf, bufSize, "\"%s\":%lu", p->name, (unsigned long)v);
            break;
        }
        case PARAM_STRING:
            n = snprintf(buf, bufSize, "\"%s\":\"%s\"",
                         p->name, (const char *)p->ptr);
            break;
    }
    return (n > 0 && n < bufSize) ? n : 0;
}

/* Look up a param by name. Returns index or -1 if not found. */
static inline int paramFind(const ParamDef *table, int count, const char *name)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(table[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* ─── paramGet ───────────────────────────────────────────────────────────── */

/*
 * Get a single parameter value as JSON.
 *   Success: {"txpwr":14}  or  {"nodeid":"ab01"}
 *   Error:   {"e":"unknown param"}
 * Returns bytes written (excluding null), or 0 on buffer overflow.
 */
static inline int paramGet(const ParamDef *table, int count,
                           const char *name, char *buf, int bufSize)
{
    int idx = paramFind(table, count, name);
    if (idx < 0) {
        int n = snprintf(buf, bufSize, "{\"e\":\"unknown param\"}");
        return (n > 0 && n < bufSize) ? n : 0;
    }

    /* Build {<kv>} using paramFmtKV */
    buf[0] = '{';
    int kvLen = paramFmtKV(&table[idx], buf + 1, bufSize - 1);
    if (kvLen == 0) return 0;
    int pos = 1 + kvLen;
    if (pos + 2 > bufSize) return 0;
    buf[pos++] = '}';
    buf[pos]   = '\0';
    return pos;
}

/* ─── paramSet ───────────────────────────────────────────────────────────── */

/*
 * Set a parameter from a string value.
 *   Success:    {"txpwr":14}               (echoes new value)
 *   Not found:  {"e":"unknown param"}
 *   Read-only:  {"e":"read-only: nodeid"}
 *   Range:      {"e":"range: -17..22"}
 * Returns bytes written (excluding null), or 0 on buffer overflow.
 */
static inline int paramSet(const ParamDef *table, int count,
                           const char *name, const char *valueStr,
                           char *buf, int bufSize)
{
    int idx = paramFind(table, count, name);
    if (idx < 0) {
        int n = snprintf(buf, bufSize, "{\"e\":\"unknown param\"}");
        return (n > 0 && n < bufSize) ? n : 0;
    }

    const ParamDef *p = &table[idx];

    if (!p->writable) {
        int n = snprintf(buf, bufSize, "{\"e\":\"read-only: %s\"}", name);
        return (n > 0 && n < bufSize) ? n : 0;
    }

    if (p->type == PARAM_STRING) {
        /* String set not supported (all current string params are read-only) */
        int n = snprintf(buf, bufSize, "{\"e\":\"read-only: %s\"}", name);
        return (n > 0 && n < bufSize) ? n : 0;
    }

    /* Parse and apply value based on type — use memcpy for alignment safety */
    if (p->type == PARAM_UINT32) {
        /* UINT32 uses strtoul and skips int16 range check */
        uint32_t v32 = (uint32_t)strtoul(valueStr, NULL, 10);
        memcpy(p->ptr, &v32, sizeof(v32));
    } else {
        /* Parse numeric value */
        long val = strtol(valueStr, NULL, 10);

        /* Range check (only for types that fit in int16 range) */
        if (val < p->minVal || val > p->maxVal) {
            int n = snprintf(buf, bufSize, "{\"e\":\"range: %d..%d\"}",
                             p->minVal, p->maxVal);
            return (n > 0 && n < bufSize) ? n : 0;
        }

        /* Apply value */
        switch (p->type) {
            case PARAM_INT8:   { int8_t v   = (int8_t)val;   memcpy(p->ptr, &v, sizeof(v)); break; }
            case PARAM_UINT8:  { uint8_t v  = (uint8_t)val;  memcpy(p->ptr, &v, sizeof(v)); break; }
            case PARAM_INT16:  { int16_t v  = (int16_t)val;  memcpy(p->ptr, &v, sizeof(v)); break; }
            case PARAM_UINT16: { uint16_t v = (uint16_t)val; memcpy(p->ptr, &v, sizeof(v)); break; }
            default: break;
        }
    }

    /* Call onSet callback if present */
    if (p->onSet) p->onSet(name);

    /* Return confirmation (same format as paramGet) */
    return paramGet(table, count, name, buf, bufSize);
}

/* ─── paramsList ─────────────────────────────────────────────────────────── */

/*
 * List parameters with values, paginated.
 *   {"m":0,"p":{"rxduty":90,"txpwr":14}}
 *   {"m":1,"p":{"nodeid":"ab01","nodev":1}}   (more pages remain)
 *
 * offset: index of first param to include (0 for first page).
 * Greedy packing: includes as many params as fit, sets "m":1 if more remain.
 * Returns bytes written (excluding null), or 0 on buffer overflow.
 */
static inline int paramsList(const ParamDef *table, int count,
                             int offset, char *buf, int bufSize)
{
    /* Minimum output: {"m":0,"p":{}} = 15 chars + null */
    if (bufSize < 16) return 0;

    /* Write prefix: {"m":0,"p":{ */
    int pos = snprintf(buf, bufSize, "{\"m\":0,\"p\":{");
    const int morePos = 5;  /* position of '0' in {"m":0 — overwrite to '1' if needed */

    if (offset < 0) offset = 0;

    bool first = true;
    int i;
    for (i = offset; i < count; i++) {
        /* Format this param's "key":value into a temp buffer */
        char item[80];
        int itemLen = paramFmtKV(&table[i], item, sizeof(item));
        if (itemLen == 0) continue;  /* shouldn't happen, but skip on error */

        /* Space needed: item + optional comma + closing }} + null */
        int need = itemLen + (first ? 0 : 1) + 2 + 1;
        if (pos + need > bufSize) {
            buf[morePos] = '1';  /* more pages remain */
            break;
        }

        if (!first) buf[pos++] = ',';
        memcpy(buf + pos, item, itemLen);
        pos += itemLen;
        first = false;
    }

    /* Close: }} */
    buf[pos++] = '}';
    buf[pos++] = '}';
    buf[pos]   = '\0';

    return pos;
}

/* ─── cmdsList ───────────────────────────────────────────────────────────── */

/*
 * List command names, paginated.
 *   {"c":["blink","discover","echo"],"m":0}
 *
 * cmdNames must be pre-sorted alphabetically.
 * offset: index of first command to include (0 for first page).
 * Returns bytes written (excluding null), or 0 on buffer overflow.
 */
static inline int cmdsList(const char **cmdNames, int cmdCount,
                           int offset, char *buf, int bufSize)
{
    /* Minimum output: {"c":[],"m":0} = 14 chars + null */
    if (bufSize < 15) return 0;

    /* Write prefix: {"c":[ */
    int pos = snprintf(buf, bufSize, "{\"c\":[");

    if (offset < 0) offset = 0;

    bool first = true;
    int i;
    for (i = offset; i < cmdCount; i++) {
        int nameLen = strlen(cmdNames[i]);

        /* Space needed: "name" + optional comma + closing ],"m":0} + null */
        int need = nameLen + 2 + (first ? 0 : 1) + 8 + 1;
        if (pos + need > bufSize) {
            break;
        }

        if (!first) buf[pos++] = ',';
        buf[pos++] = '"';
        memcpy(buf + pos, cmdNames[i], nameLen);
        pos += nameLen;
        buf[pos++] = '"';
        first = false;
    }

    /* Close: ],"m":X} */
    int more = (i < cmdCount) ? 1 : 0;
    pos += snprintf(buf + pos, bufSize - pos, "],\"m\":%d}", more);

    return pos;
}

/* ─── paramsApplyStaged ──────────────────────────────────────────────────── */

/*
 * Copy staged (radio) params from their cfg fields to runtime globals.
 * Only copies params where runtimePtr != NULL.
 * Called by rcfg_radio after setparam updates cfg fields.
 */
static inline void paramsApplyStaged(const ParamDef *table, int count)
{
    for (int i = 0; i < count; i++) {
        if (table[i].runtimePtr == NULL) continue;
        switch (table[i].type) {
            case PARAM_INT8:   memcpy(table[i].runtimePtr, table[i].ptr, sizeof(int8_t));   break;
            case PARAM_UINT8:  memcpy(table[i].runtimePtr, table[i].ptr, sizeof(uint8_t));  break;
            case PARAM_INT16:  memcpy(table[i].runtimePtr, table[i].ptr, sizeof(int16_t));  break;
            case PARAM_UINT16: memcpy(table[i].runtimePtr, table[i].ptr, sizeof(uint16_t)); break;
            case PARAM_UINT32: memcpy(table[i].runtimePtr, table[i].ptr, sizeof(uint32_t)); break;
            case PARAM_STRING: break;
        }
    }
}

/* ─── paramsSyncToConfig ─────────────────────────────────────────────────── */

/*
 * Copy runtime parameter values into a NodeConfig struct for EEPROM persistence.
 * Only copies params where cfgOffset != CFG_OFFSET_NONE.
 * After calling, use cfgSave(cfg) to write to EEPROM.
 */
static inline void paramsSyncToConfig(const ParamDef *table, int count,
                                      NodeConfig *cfg)
{
    for (int i = 0; i < count; i++) {
        if (table[i].cfgOffset == CFG_OFFSET_NONE) continue;

        uint8_t *dst = (uint8_t *)cfg + table[i].cfgOffset;

        /* memcpy both sides for alignment safety (packed struct offsets) */
        switch (table[i].type) {
            case PARAM_INT8:   memcpy(dst, table[i].ptr, sizeof(int8_t));   break;
            case PARAM_UINT8:  memcpy(dst, table[i].ptr, sizeof(uint8_t));  break;
            case PARAM_INT16:  memcpy(dst, table[i].ptr, sizeof(int16_t));  break;
            case PARAM_UINT16: memcpy(dst, table[i].ptr, sizeof(uint16_t)); break;
            case PARAM_UINT32: memcpy(dst, table[i].ptr, sizeof(uint32_t)); break;
            case PARAM_STRING: /* string params are read-only, skip */      break;
        }
    }
}

#endif /* PARAMS_H */
