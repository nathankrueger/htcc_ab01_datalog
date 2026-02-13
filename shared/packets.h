#ifndef PACKETS_H
#define PACKETS_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ─── Debug (no-op unless defined before include) ─────────────────────────── */
#ifndef CDBG
#define CDBG(fmt, ...) ((void)0)
#endif

/* ─── Configuration ──────────────────────────────────────────────────────── */

#ifndef LORA_MAX_PAYLOAD
#define LORA_MAX_PAYLOAD 250
#endif

/* ─── CRC-32 ─────────────────────────────────────────────────────────────── */

/*
 * Standard CRC-32 (ISO 3309 / ITU-T V.42) — identical to Python zlib.crc32().
 * Bit-reversed polynomial 0xEDB88320.
 */
static const uint32_t CRC32_TABLE[256] = {
    0x00000000u,0x77073096u,0xEE0E612Cu,0x990951BAu,
    0x076DC419u,0x706AF48Fu,0xE963A535u,0x9E6495A3u,
    0x0EDB8832u,0x79DCB8A4u,0xE0D5E91Eu,0x97D2D988u,
    0x09B64C2Bu,0x7EB17CBDu,0xE7B82D07u,0x90BF1D91u,
    0x1DB71064u,0x6AB020F2u,0xF3B97148u,0x84BE41DEu,
    0x1ADAD47Du,0x6DDDE4EBu,0xF4D4B551u,0x83D385C7u,
    0x136C9856u,0x646BA8C0u,0xFD62F97Au,0x8A65C9ECu,
    0x14015C4Fu,0x63066CD9u,0xFA0F3D63u,0x8D080DF5u,
    0x3B6E20C8u,0x4C69105Eu,0xD56041E4u,0xA2677172u,
    0x3C03E4D1u,0x4B04D447u,0xD20D85FDu,0xA50AB56Bu,
    0x35B5A8FAu,0x42B2986Cu,0xDBBBC9D6u,0xACBCF940u,
    0x32D86CE3u,0x45DF5C75u,0xDCD60DCFu,0xABD13D59u,
    0x26D930ACu,0x51DE003Au,0xC8D75180u,0xBFD06116u,
    0x21B4F4B5u,0x56B3C423u,0xCFBA9599u,0xB8BDA50Fu,
    0x2802B89Eu,0x5F058808u,0xC60CD9B2u,0xB10BE924u,
    0x2F6F7C87u,0x58684C11u,0xC1611DABu,0xB6662D3Du,
    0x76DC4190u,0x01DB7106u,0x98D220BCu,0xEFD5102Au,
    0x71B18589u,0x06B6B51Fu,0x9FBFE4A5u,0xE8B8D433u,
    0x7807C9A2u,0x0F00F934u,0x9609A88Eu,0xE10E9818u,
    0x7F6A0DBBu,0x086D3D2Du,0x91646C97u,0xE6635C01u,
    0x6B6B51F4u,0x1C6C6162u,0x856530D8u,0xF262004Eu,
    0x6C0695EDu,0x1B01A57Bu,0x8208F4C1u,0xF50FC457u,
    0x65B0D9C6u,0x12B7E950u,0x8BBEB8EAu,0xFCB9887Cu,
    0x62DD1DDFu,0x15DA2D49u,0x8CD37CF3u,0xFBD44C65u,
    0x4DB26158u,0x3AB551CEu,0xA3BC0074u,0xD4BB30E2u,
    0x4ADFA541u,0x3DD895D7u,0xA4D1C46Du,0xD3D6F4FBu,
    0x4369E96Au,0x346ED9FCu,0xAD678846u,0xDA60B8D0u,
    0x44042D73u,0x33031DE5u,0xAA0A4C5Fu,0xDD0D7CC9u,
    0x5005713Cu,0x270241AAu,0xBE0B1010u,0xC90C2086u,
    0x5768B525u,0x206F85B3u,0xB966D409u,0xCE61E49Fu,
    0x5EDEF90Eu,0x29D9C998u,0xB0D09822u,0xC7D7A8B4u,
    0x59B33D17u,0x2EB40D81u,0xB7BD5C3Bu,0xC0BA6CADu,
    0xEDB88320u,0x9ABFB3B6u,0x03B6E20Cu,0x74B1D29Au,
    0xEAD54739u,0x9DD277AFu,0x04DB2615u,0x73DC1683u,
    0xE3630B12u,0x94643B84u,0x0D6D6A3Eu,0x7A6A5AA8u,
    0xE40ECF0Bu,0x9309FF9Du,0x0A00AE27u,0x7D079EB1u,
    0xF00F9344u,0x8708A3D2u,0x1E01F268u,0x6906C2FEu,
    0xF762575Du,0x806567CBu,0x196C3671u,0x6E6B06E7u,
    0xFED41B76u,0x89D32BE0u,0x10DA7A5Au,0x67DD4ACCu,
    0xF9B9DF6Fu,0x8EBEEFF9u,0x17B7BE43u,0x60B08ED5u,
    0xD6D6A3E8u,0xA1D1937Eu,0x38D8C2C4u,0x4FDFF252u,
    0xD1BB67F1u,0xA6BC5767u,0x3FB506DDu,0x48B2364Bu,
    0xD80D2BDAu,0xAF0A1B4Cu,0x36034AF6u,0x41047A60u,
    0xDF60EFC3u,0xA867DF55u,0x316E8EEFu,0x4669BE79u,
    0xCB61B38Cu,0xBC66831Au,0x256FD2A0u,0x5268E236u,
    0xCC0C7795u,0xBB0B4703u,0x220216B9u,0x5505262Fu,
    0xC5BA3BBEu,0xB2BD0B28u,0x2BB45A92u,0x5CB36A04u,
    0xC2D7FFA7u,0xB5D0CF31u,0x2CD99E8Bu,0x5BDEAE1Du,
    0x9B64C2B0u,0xEC63F226u,0x756AA39Cu,0x026D930Au,
    0x9C0906A9u,0xEB0E363Fu,0x72076785u,0x05005713u,
    0x95BF4A82u,0xE2B87A14u,0x7BB12BAEu,0x0CB61B38u,
    0x92D28E9Bu,0xE5D5BE0Du,0x7CDCEFB7u,0x0BDBDF21u,
    0x86D3D2D4u,0xF1D4E242u,0x68DDB3F8u,0x1FDA836Eu,
    0x81BE16CDu,0xF6B9265Bu,0x6FB077E1u,0x18B74777u,
    0x88085AE6u,0xFF0F6A70u,0x66063BCAu,0x11010B5Cu,
    0x8F659EFFu,0xF862AE69u,0x616BFFD3u,0x166CCF45u,
    0xA00AE278u,0xD70DD2EEu,0x4E048354u,0x3903B3C2u,
    0xA7672661u,0xD06016F7u,0x4969474Du,0x3E6E77DBu,
    0xAED16A4Au,0xD9D65ADCu,0x40DF0B66u,0x37D83BF0u,
    0xA9BCAE53u,0xDEBB9EC5u,0x47B2CF7Fu,0x30B5FFE9u,
    0xBDBDF21Cu,0xCABAC28Au,0x53B39330u,0x24B4A3A6u,
    0xBAD03605u,0xCDD70693u,0x54DE5729u,0x23D967BFu,
    0xB3667A2Eu,0xC4614AB8u,0x5D681B02u,0x2A6F2B94u,
    0xB40BBE37u,0xC30C8EA1u,0x5A05DF1Bu,0x2D02EF8Du,
};

static inline uint32_t crc32_compute(const char *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = CRC32_TABLE[(crc ^ (uint8_t)data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

/* ─── Sensor Reading Types ───────────────────────────────────────────────── */

typedef struct {
    const char *name;   /* reading name, e.g. "Temperature" */
    int         sid;    /* sensor class ID from the Python registry */
    const char *units;  /* units as a JSON string value */
    double      value;  /* double for full GPS precision */
} Reading;

/* ─── Sensor Packet Builder ──────────────────────────────────────────────── */

/*
 * Format a double for JSON, matching Python's json.dumps round-trip output.
 * Uses 8 significant digits (~1mm GPS accuracy) to fit in LoRa packets (250 byte limit).
 * Strips trailing zeros after decimal point.
 */
static inline int fmtVal(char *buf, size_t cap, double val)
{
    int len = snprintf(buf, cap, "%.8g", val);

    char *dot = strchr(buf, '.');
    if (dot) {
        char *p = buf + len - 1;
        while (p > dot && *p == '0') { *p-- = '\0'; len--; }
        if (p == dot)                { *p  = '\0'; len--; }
    }
    return len;
}

/*
 * Build one LoRa packet containing readings[0 .. count-1].
 *
 * The CRC is computed over the JSON with ALL keys sorted alphabetically and
 * the "c" field absent.  Key sort orders:
 *   top-level:  n  <  r  <  t
 *   per-reading: k  <  s  <  u  <  v
 *
 * Returns the byte length written to *buf, or 0 on overflow.
 */
static inline int buildSensorPacket(char *buf, size_t bufCap,
                                    const char *nodeId, uint32_t ts,
                                    const Reading *readings, int count)
{
    /* 1. Serialise the readings array (keys already in sorted order) */
    char rBuf[256];
    int  rLen = 0;
    rBuf[rLen++] = '[';

    for (int i = 0; i < count; i++) {
        if (i > 0) rBuf[rLen++] = ',';

        char valStr[24];
        fmtVal(valStr, sizeof(valStr), readings[i].value);

        rLen += snprintf(rBuf + rLen, sizeof(rBuf) - rLen,
                         "{\"k\":\"%s\",\"s\":%d,\"u\":\"%s\",\"v\":%s}",
                         readings[i].name,
                         readings[i].sid,
                         readings[i].units,
                         valStr);
        if (rLen >= (int)sizeof(rBuf) - 1) return 0;
    }
    rBuf[rLen++] = ']';
    rBuf[rLen]   = '\0';

    /* 2. Build the CRC payload string: sorted top-level keys  n < r < t */
    char crcBuf[LORA_MAX_PAYLOAD + 64];
    int  cLen = snprintf(crcBuf, sizeof(crcBuf),
                         "{\"n\":\"%s\",\"r\":%s,\"t\":%u}",
                         nodeId, rBuf, ts);
    if (cLen <= 0) return 0;

    /* 3. CRC-32 over that exact byte string */
    uint32_t crc = crc32_compute(crcBuf, (size_t)cLen);

    /* 4. Final packet with padding for ASR650x TX-FIFO workaround */
    buf[0] = buf[1] = buf[2] = buf[3] = ' ';
    int pLen = snprintf(buf + 4, bufCap - 4,
                        "{\"n\":\"%s\",\"r\":%s,\"t\":%u,\"c\":\"%08x\"}",
                        nodeId, rBuf, ts, crc);
    if (pLen <= 0 || pLen >= (int)(bufCap - 4)) return 0;
    return pLen + 4;
}

/* ─── Command Packet Types ───────────────────────────────────────────────── */

#define CMD_MAX_ARGS     4
#define CMD_MAX_ARG_LEN  163  /* Max echo: CMD_RESPONSE_BUF_SIZE - 8 for {"r":""} wrapper */
#define CMD_MAX_NAME_LEN 32
#define NODE_ID_MAX_LEN  16

typedef struct {
    char     cmd[CMD_MAX_NAME_LEN];
    char     args[CMD_MAX_ARGS][CMD_MAX_ARG_LEN];
    int      arg_count;
    char     node_id[NODE_ID_MAX_LEN];
    uint32_t timestamp;
    char     crc[9];  /* 8 hex chars + null */
} CommandPacket;

/* ─── JSON Parsing Helpers ───────────────────────────────────────────────── */

/*
 * Extract a string value for a given key from JSON.
 * Returns true if found, copies value to out (up to outCap-1 chars).
 */
static inline bool extractJsonString(const char *json, const char *key,
                                     char *out, size_t outCap)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);

    const char *start = strstr(json, pattern);
    if (!start) return false;

    start += strlen(pattern);
    const char *end = strchr(start, '"');
    if (!end) return false;

    size_t len = end - start;
    if (len >= outCap) len = outCap - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

/*
 * Extract an integer value for a given key from JSON.
 * Returns true if found.
 */
static inline bool extractJsonInt(const char *json, const char *key,
                                  int32_t *out)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);

    const char *start = strstr(json, pattern);
    if (!start) return false;

    start += strlen(pattern);
    /* Skip whitespace */
    while (*start == ' ') start++;

    char *endptr;
    long val = strtol(start, &endptr, 10);
    if (endptr == start) return false;

    *out = (int32_t)val;
    return true;
}

/*
 * Extract a string array for a given key from JSON.
 * Supports arrays like: "a":["arg1","arg2"] or "a":[]
 * Returns number of elements found (0 if empty array, -1 if not found/error).
 */
static inline int extractJsonStringArray(const char *json, const char *key,
                                         char out[][CMD_MAX_ARG_LEN], int maxCount)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":[", key);

    const char *start = strstr(json, pattern);
    if (!start) return -1;

    start += strlen(pattern);

    /* Skip whitespace */
    while (*start == ' ') start++;

    /* Empty array? */
    if (*start == ']') return 0;

    int count = 0;
    while (count < maxCount) {
        /* Expect opening quote */
        if (*start != '"') break;
        start++;

        /* Find closing quote */
        const char *end = strchr(start, '"');
        if (!end) break;

        size_t len = end - start;
        if (len >= CMD_MAX_ARG_LEN) len = CMD_MAX_ARG_LEN - 1;
        memcpy(out[count], start, len);
        out[count][len] = '\0';
        count++;

        start = end + 1;
        /* Skip whitespace */
        while (*start == ' ') start++;

        /* More elements? */
        if (*start == ',') {
            start++;
            while (*start == ' ') start++;
        } else if (*start == ']') {
            break;
        } else {
            break;
        }
    }

    return count;
}

/* ─── Command Packet Parser ──────────────────────────────────────────────── */

/*
 * Parse and verify a command packet.
 *
 * Command format (keys sorted for CRC):
 *   {"a":[],"c":"...","cmd":"...","n":"...","t":"cmd","ts":...}
 *
 * CRC is computed over JSON with "c" field removed, keys sorted:
 *   {"a":[],"cmd":"...","n":"...","t":"cmd","ts":...}
 *
 * Returns true if valid command with matching CRC.
 */
static inline bool parseCommand(const uint8_t *data, size_t len, CommandPacket *out)
{
    if (len == 0 || len > LORA_MAX_PAYLOAD) {
        CDBG("PARSE_FAIL len=%zu\n", len);
        return false;
    }

    /* Null-terminate for string operations */
    char json[LORA_MAX_PAYLOAD + 1];
    memcpy(json, data, len);
    json[len] = '\0';

    CDBG("PARSE_JSON: %s\n", json);

    /* Verify this is a command packet */
    if (!strstr(json, "\"t\":\"cmd\"")) {
        CDBG("PARSE_FAIL no_cmd_type\n");
        return false;
    }

    /* Extract fields */
    if (!extractJsonString(json, "cmd", out->cmd, sizeof(out->cmd))) {
        CDBG("PARSE_FAIL no_cmd\n");
        return false;
    }

    if (!extractJsonString(json, "c", out->crc, sizeof(out->crc))) {
        CDBG("PARSE_FAIL no_crc\n");
        return false;
    }

    /* node_id can be empty string for broadcast */
    if (!extractJsonString(json, "n", out->node_id, sizeof(out->node_id))) {
        out->node_id[0] = '\0';
    }

    int32_t ts;
    if (!extractJsonInt(json, "ts", &ts)) {
        CDBG("PARSE_FAIL no_ts\n");
        return false;
    }
    out->timestamp = (uint32_t)ts;

    /* Extract args array */
    out->arg_count = extractJsonStringArray(json, "a", out->args, CMD_MAX_ARGS);
    if (out->arg_count < 0) out->arg_count = 0;

    CDBG("PARSE_FIELDS cmd=%s n=%s ts=%u argc=%d\n",
         out->cmd, out->node_id, out->timestamp, out->arg_count);

    /*
     * Verify CRC: rebuild JSON with sorted keys (excluding "c")
     * Key order: a < cmd < n < t < ts
     */
    char crcBuf[LORA_MAX_PAYLOAD];
    int pos = 0;

    /* Build args array string - must fit CMD_MAX_ARGS args of CMD_MAX_ARG_LEN each */
    char argsBuf[LORA_MAX_PAYLOAD];
    int aLen = 0;
    argsBuf[aLen++] = '[';
    for (int i = 0; i < out->arg_count; i++) {
        if (i > 0) argsBuf[aLen++] = ',';
        aLen += snprintf(argsBuf + aLen, sizeof(argsBuf) - aLen,
                         "\"%s\"", out->args[i]);
    }
    argsBuf[aLen++] = ']';
    argsBuf[aLen] = '\0';

    pos = snprintf(crcBuf, sizeof(crcBuf),
                   "{\"a\":%s,\"cmd\":\"%s\",\"n\":\"%s\",\"t\":\"cmd\",\"ts\":%u}",
                   argsBuf, out->cmd, out->node_id, out->timestamp);

    uint32_t computed = crc32_compute(crcBuf, pos);
    char computedHex[9];
    snprintf(computedHex, sizeof(computedHex), "%08x", computed);

    CDBG("PARSE_CRC crcBuf=%s\n", crcBuf);
    CDBG("PARSE_CRC computed=%s expected=%s\n", computedHex, out->crc);

    if (strcmp(computedHex, out->crc) != 0) {
        CDBG("PARSE_FAIL crc_mismatch\n");
        return false;
    }

    return true;
}

/* ─── ACK Packet Builder ─────────────────────────────────────────────────── */

/*
 * Build an ACK packet for a received command.
 *
 * ACK format (keys sorted for CRC):
 *   {"c":"...","id":"...","n":"...","t":"ack"}
 *
 * CRC is computed over JSON with "c" field removed, keys sorted:
 *   {"id":"...","n":"...","t":"ack"}
 *
 * Command ID format: "{timestamp}_{crc_first_4_chars}"
 *
 * Returns length written to buf, or 0 on error.
 */
static inline int buildAckPacket(char *buf, size_t bufCap,
                                 uint32_t cmdTimestamp, const char *cmdCrc,
                                 const char *nodeId)
{
    /* Build command ID: timestamp_crcprefix */
    char commandId[32];
    snprintf(commandId, sizeof(commandId), "%u_%.4s", cmdTimestamp, cmdCrc);

    /* Build CRC payload (sorted keys, no "c"): id < n < t */
    char crcBuf[128];
    int cLen = snprintf(crcBuf, sizeof(crcBuf),
                        "{\"id\":\"%s\",\"n\":\"%s\",\"t\":\"ack\"}",
                        commandId, nodeId);

    uint32_t crc = crc32_compute(crcBuf, cLen);

    /* Build final packet with padding for ASR650x TX-FIFO workaround */
    buf[0] = buf[1] = buf[2] = buf[3] = ' ';
    int pLen = snprintf(buf + 4, bufCap - 4,
                        "{\"c\":\"%08x\",\"id\":\"%s\",\"n\":\"%s\",\"t\":\"ack\"}",
                        crc, commandId, nodeId);

    if (pLen <= 0 || pLen >= (int)(bufCap - 4)) return 0;
    return pLen + 4;
}

/*
 * Build an ACK packet with optional payload for command responses.
 *
 * ACK format with payload (keys sorted for CRC):
 *   {"c":"...","id":"...","n":"...","p":{...},"t":"ack"}
 *
 * CRC is computed over JSON with "c" field removed, keys sorted:
 *   {"id":"...","n":"...","p":{...},"t":"ack"}
 *
 * payload: JSON object string (without outer braces added), or NULL/empty for no payload
 *
 * Returns length written to buf, or 0 on error.
 */
static inline int buildAckPacketWithPayload(char *buf, size_t bufCap,
                                            uint32_t cmdTimestamp, const char *cmdCrc,
                                            const char *nodeId, const char *payload)
{
    /* If no payload, use the simpler function */
    if (payload == NULL || payload[0] == '\0') {
        return buildAckPacket(buf, bufCap, cmdTimestamp, cmdCrc, nodeId);
    }

    /* Build command ID: timestamp_crcprefix */
    char commandId[32];
    snprintf(commandId, sizeof(commandId), "%u_%.4s", cmdTimestamp, cmdCrc);

    /* Build CRC payload (sorted keys, no "c"): id < n < p < t */
    char crcBuf[LORA_MAX_PAYLOAD];
    int cLen = snprintf(crcBuf, sizeof(crcBuf),
                        "{\"id\":\"%s\",\"n\":\"%s\",\"p\":%s,\"t\":\"ack\"}",
                        commandId, nodeId, payload);

    if (cLen <= 0 || cLen >= (int)sizeof(crcBuf)) return 0;

    uint32_t crc = crc32_compute(crcBuf, cLen);

    /* Build final packet with padding for ASR650x TX-FIFO workaround */
    buf[0] = buf[1] = buf[2] = buf[3] = ' ';
    int pLen = snprintf(buf + 4, bufCap - 4,
                        "{\"c\":\"%08x\",\"id\":\"%s\",\"n\":\"%s\",\"p\":%s,\"t\":\"ack\"}",
                        crc, commandId, nodeId, payload);

    if (pLen <= 0 || pLen >= (int)(bufCap - 4)) return 0;
    return pLen + 4;
}

/* ─── Command Callback Registry ──────────────────────────────────────────── */

typedef enum {
    CMD_SCOPE_BROADCAST = 0,  /* Only respond to broadcast (node_id = "") */
    CMD_SCOPE_PRIVATE   = 1,  /* Only respond to targeted (node_id = self) */
    CMD_SCOPE_ANY       = 2   /* Respond to both */
} CommandScope;

/* Callback signature: receives command name and args */
typedef void (*CommandCallback)(const char *cmd, char args[][CMD_MAX_ARG_LEN], int arg_count);

#define CMD_REGISTRY_MAX 16

typedef struct {
    const char      *cmd;       /* Command name to match */
    CommandCallback  callback;
    CommandScope     scope;
    bool             earlyAck;  /* true = ACK before handler, false = after (for responses) */
    bool             ackJitter; /* true = random delay before ACK (for discovery) */
} CommandHandler;

typedef struct {
    CommandHandler handlers[CMD_REGISTRY_MAX];
    int            count;
    const char    *node_id;     /* This node's ID for private matching */
} CommandRegistry;

static inline void cmdRegistryInit(CommandRegistry *reg, const char *nodeId)
{
    reg->count = 0;
    reg->node_id = nodeId;
}

/*
 * Register a command handler.
 * earlyAck: true = send ACK before handler runs (default for most commands)
 *           false = send ACK after handler runs (for commands that return data)
 * Returns true if registered, false if registry full.
 */
static inline bool cmdRegister(CommandRegistry *reg, const char *cmd,
                               CommandCallback callback, CommandScope scope,
#ifdef __cplusplus
                               bool earlyAck, bool ackJitter = false)
#else
                               bool earlyAck, bool ackJitter)
#endif
{
    if (reg->count >= CMD_REGISTRY_MAX) return false;

    reg->handlers[reg->count].cmd = cmd;
    reg->handlers[reg->count].callback = callback;
    reg->handlers[reg->count].scope = scope;
    reg->handlers[reg->count].earlyAck = earlyAck;
    reg->handlers[reg->count].ackJitter = ackJitter;
    reg->count++;
    return true;
}

/*
 * Shared response buffer for command handlers.
 * Handlers that need to return data (earlyAck=false) write JSON here.
 * Cleared before each dispatch; included in ACK payload if non-empty.
 */
/*
 * Maximum payload size for ACK response packets.
 *
 * ACK with payload format:
 *   {"c":"XXXXXXXX","id":"ts_crc4","n":"nodeId","p":PAYLOAD,"t":"ack"}
 *
 * Overhead breakdown:
 *   4  - TX-FIFO padding (ASR650x workaround)
 *   6  - {"c":"
 *   8  - CRC hex value
 *   8  - ","id":"
 *  15  - commandId max (10-digit timestamp + _ + 4-char CRC prefix)
 *   6  - ","n":"
 *  16  - NODE_ID_MAX_LEN
 *   5  - ","p":
 *  11  - ,"t":"ack"}
 * ----
 *  79  total overhead
 */
#define ACK_PAYLOAD_OVERHEAD (4 + 6 + 8 + 8 + 15 + 6 + NODE_ID_MAX_LEN + 5 + 11)
#define CMD_RESPONSE_BUF_SIZE (LORA_MAX_PAYLOAD - ACK_PAYLOAD_OVERHEAD)

/*
 * Look up a command handler by name and scope.
 * Returns pointer to handler if found, NULL otherwise.
 */
static inline CommandHandler* cmdLookup(CommandRegistry *reg, const CommandPacket *pkt)
{
    bool is_broadcast = (pkt->node_id[0] == '\0');
    bool is_for_me = (strcmp(pkt->node_id, reg->node_id) == 0);

    /* If not broadcast and not for us, no match */
    if (!is_broadcast && !is_for_me) return NULL;

    for (int i = 0; i < reg->count; i++) {
        CommandHandler *h = &reg->handlers[i];

        /* Check command name matches */
        if (strcmp(h->cmd, pkt->cmd) != 0) continue;

        /* Check scope */
        bool scope_ok = false;
        switch (h->scope) {
            case CMD_SCOPE_ANY:
                scope_ok = true;
                break;
            case CMD_SCOPE_BROADCAST:
                scope_ok = is_broadcast;
                break;
            case CMD_SCOPE_PRIVATE:
                scope_ok = is_for_me;
                break;
        }

        if (scope_ok) return h;
    }

    return NULL;
}

/*
 * Dispatch a command to registered handlers.
 * Returns true if at least one handler was called.
 */
static inline bool cmdDispatch(CommandRegistry *reg, const CommandPacket *pkt)
{
    bool is_broadcast = (pkt->node_id[0] == '\0');
    bool is_for_me = (strcmp(pkt->node_id, reg->node_id) == 0);

    /* If not broadcast and not for us, ignore */
    if (!is_broadcast && !is_for_me) return false;

    bool handled = false;

    for (int i = 0; i < reg->count; i++) {
        CommandHandler *h = &reg->handlers[i];

        /* Check command name matches */
        if (strcmp(h->cmd, pkt->cmd) != 0) continue;

        /* Check scope */
        bool scope_ok = false;
        switch (h->scope) {
            case CMD_SCOPE_ANY:
                scope_ok = true;
                break;
            case CMD_SCOPE_BROADCAST:
                scope_ok = is_broadcast;
                break;
            case CMD_SCOPE_PRIVATE:
                scope_ok = is_for_me;
                break;
        }

        if (scope_ok) {
            /* Cast away const for callback (args are copies anyway) */
            h->callback(pkt->cmd, (char (*)[CMD_MAX_ARG_LEN])pkt->args, pkt->arg_count);
            handled = true;
        }
    }

    return handled;
}

#endif /* PACKETS_H */
