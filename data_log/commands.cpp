/*
 * commands.cpp — Command handlers and parameter table for data_log sketch
 *
 * All command handler functions, the ParamDef table, onSet callbacks,
 * radio config helpers, and the sorted command name list live here.
 * data_log.ino calls commandsInit() once from setup().
 */

#include "Arduino.h"
#include "LoRaWan_APP.h"
#include "commands.h"
#include "radio.h"
#include "config.h"
#include "params.h"
#include "led.h"

/* ─── Debug Output ──────────────────────────────────────────────────────── */

#ifndef DEBUG
#define DEBUG 1
#endif

#if DEBUG
  #define DBG(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
  #define DBGLN(msg)     Serial.println(msg)
  #define DBGP(msg)      Serial.print(msg)
#else
  #define DBG(fmt, ...)  ((void)0)
  #define DBGLN(msg)     ((void)0)
  #define DBGP(msg)      ((void)0)
#endif

/* ─── Shared Response Buffer ────────────────────────────────────────────── */

char cmdResponseBuf[CMD_RESPONSE_BUF_SIZE];

/* ─── Radio Config Helpers ──────────────────────────────────────────────── */

void applyTxConfig(void)
{
    Radio.SetTxConfig(MODEM_LORA, txPower, 0, loraBW,
                      spreadFactor, LORA_CODINGRATE,
                      LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                      true, 0, 0, LORA_IQ_INVERSION_ON, 3000);
}

void applyRxConfig(void)
{
    Radio.SetRxConfig(MODEM_LORA, loraBW, spreadFactor,
                      LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                      LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                      0, true, 0, 0, LORA_IQ_INVERSION_ON, true);
}

/* onSet callback for txpwr: re-apply TX config */
static void onSetTxPwr(const char *name)
{
    (void)name;
    applyTxConfig();
}

/* onSet callback for sf/bw: re-apply both TX and RX config */
static void onSetRadio(const char *name)
{
    (void)name;
    applyTxConfig();
    applyRxConfig();
}

/* ─── Parameter Table ───────────────────────────────────────────────────── */

/*
 * Parameter registry — MUST be in alphabetical order by name.
 * Adding a new tunable parameter is a single row addition here.
 *
 * Fields: name, type, ptr, min, max, writable, onSet, cfgOffset
 *   cfgOffset = offsetof(NodeConfig, field) for EEPROM-persisted params
 *   cfgOffset = CFG_OFFSET_NONE (0xFF) for read-only / non-persisted params
 */
static const uint16_t nodeVersion = NODE_VERSION;

static const ParamDef paramTable[] = {
    { "bw",      PARAM_UINT8,  &loraBW,           0,   2, true,  onSetRadio, offsetof(NodeConfig, bandwidth)        },
    { "nodeid",  PARAM_STRING, cfg.nodeId,         0,   0, false, NULL,       CFG_OFFSET_NONE                        },
    { "nodev",   PARAM_UINT16, (void *)&nodeVersion, 0, 0, false, NULL,       CFG_OFFSET_NONE                        },
    { "rxduty",  PARAM_UINT8,  &rxDutyPercent,     0, 100, true,  NULL,       offsetof(NodeConfig, rxDutyPercent)     },
    { "sf",      PARAM_UINT8,  &spreadFactor,      7,  12, true,  onSetRadio, offsetof(NodeConfig, spreadingFactor)   },
    { "txpwr",   PARAM_INT8,   &txPower,         -17,  22, true,  onSetTxPwr, offsetof(NodeConfig, txOutputPower)     },
};
static const int PARAM_COUNT = sizeof(paramTable) / sizeof(paramTable[0]);

/* ─── Command Handlers ──────────────────────────────────────────────────── */

static void handlePing(const char *cmd, char args[][CMD_MAX_ARG_LEN], int arg_count)
{
    DBGLN("PING received");
}

static void handleBlink(const char *cmd, char args[][CMD_MAX_ARG_LEN], int arg_count)
{
    /* Require at least the color argument */
    if (arg_count < 1) {
        DBGLN("BLINK: missing color argument");
        return;
    }

    /* Parse color */
    LEDColor color = parseColor(args[0]);
    DBG("BLINK: color=%s", args[0]);

    /* Parse optional seconds argument (default 0.5s) */
    float seconds = 0.5f;
    if (arg_count >= 2) {
        seconds = strtof(args[1], NULL);
        if (seconds <= 0.0f) {
            DBGLN(" ERROR: invalid seconds value");
            return;
        }
    }
    /* Parse optional brightness argument (default LED_BRIGHTNESS) */
    uint8_t brightness = LED_BRIGHTNESS;
    if (arg_count >= 3) {
        int val = atoi(args[2]);
        if (val < 0 || val > 255) {
            DBGLN(" ERROR: brightness must be 0-255");
            return;
        }
        brightness = (uint8_t)val;
    }
    DBG(" seconds=%.2f brightness=%d\n", seconds, brightness);

    /* Turn on LED — tick loop will turn it off after the timer expires */
    ledSetColorBrightness(color, brightness);
    blinkActive = true;
    blinkOffTime = millis() + (unsigned long)(seconds * 1000.0f);
}

static void handleEcho(const char *cmd, char args[][CMD_MAX_ARG_LEN], int arg_count)
{
    if (arg_count < 1 || args[0][0] == '\0') {
        snprintf(cmdResponseBuf, CMD_RESPONSE_BUF_SIZE, "{\"r\":\"\"}");
        return;
    }
    /* Return the argument as a JSON object in the response buffer */
    snprintf(cmdResponseBuf, CMD_RESPONSE_BUF_SIZE, "{\"r\":\"%s\"}", args[0]);
    DBG("ECHO: responding with %s\n", cmdResponseBuf);
}

static void handleReset(const char *cmd, char args[][CMD_MAX_ARG_LEN], int arg_count)
{
    /* Optional delay in seconds (default 0 = immediate) */
    float seconds = 0.0f;
    if (arg_count >= 1) {
        seconds = strtof(args[0], NULL);
        if (seconds < 0.0f) seconds = 0.0f;
    }

    DBG("RESET: rebooting in %.1f s...\n", seconds);
    if (seconds > 0.0f)
        delay((unsigned long)(seconds * 1000.0f));
    delay(100);  /* let debug output flush */
    NVIC_SystemReset();
}

static void handleTestLed(const char *cmd, char args[][CMD_MAX_ARG_LEN], int arg_count)
{
    unsigned long delayMs = 5000;
    if (arg_count >= 1) {
        long val = atol(args[0]);
        if (val > 0) delayMs = (unsigned long)val;
    }
    DBG("TESTLED: cycling colors, %lums per step\n", delayMs);
    ledTest(delayMs);
}

static void handleSaveCfg(const char *cmd, char args[][CMD_MAX_ARG_LEN], int arg_count)
{
    /* Copy writable runtime params into the config struct via registry */
    paramsSyncToConfig(paramTable, PARAM_COUNT, &cfg);

    bool written = cfgSave(&cfg);
    const char *msg = written ? "saved" : "unchanged";
    snprintf(cmdResponseBuf, CMD_RESPONSE_BUF_SIZE,
             "{\"r\":\"%s\"}", msg);
    DBG("SAVECFG: %s\n", cmdResponseBuf);
}

/* ─── Generic Parameter Command Handlers ─────────────────────────────────── */

static void handleGetParam(const char *cmd, char args[][CMD_MAX_ARG_LEN], int arg_count)
{
    if (arg_count < 1) {
        snprintf(cmdResponseBuf, CMD_RESPONSE_BUF_SIZE, "{\"e\":\"missing param name\"}");
        return;
    }
    paramGet(paramTable, PARAM_COUNT, args[0], cmdResponseBuf, CMD_RESPONSE_BUF_SIZE);
    DBG("GETPARAM: %s\n", cmdResponseBuf);
}

static void handleSetParam(const char *cmd, char args[][CMD_MAX_ARG_LEN], int arg_count)
{
    if (arg_count < 2) {
        snprintf(cmdResponseBuf, CMD_RESPONSE_BUF_SIZE, "{\"e\":\"usage: name value\"}");
        return;
    }
    paramSet(paramTable, PARAM_COUNT, args[0], args[1], cmdResponseBuf, CMD_RESPONSE_BUF_SIZE);
    DBG("SETPARAM: %s\n", cmdResponseBuf);
}

static void handleGetParams(const char *cmd, char args[][CMD_MAX_ARG_LEN], int arg_count)
{
    int offset = 0;
    if (arg_count >= 1) offset = atoi(args[0]);
    paramsList(paramTable, PARAM_COUNT, offset, cmdResponseBuf, CMD_RESPONSE_BUF_SIZE);
    DBG("GETPARAMS: %s\n", cmdResponseBuf);
}

/* ─── Sorted Command Name List ──────────────────────────────────────────── */

static const char *cmdNames[CMD_REGISTRY_MAX];
static int cmdNameCount = 0;

static void buildCmdNameList(CommandRegistry *reg)
{
    cmdNameCount = 0;
    for (int i = 0; i < reg->count && cmdNameCount < CMD_REGISTRY_MAX; i++)
        cmdNames[cmdNameCount++] = reg->handlers[i].cmd;

    /* Insertion sort — alpha order for JSON CRC consistency */
    for (int i = 1; i < cmdNameCount; i++) {
        const char *key = cmdNames[i];
        int j = i - 1;
        while (j >= 0 && strcmp(cmdNames[j], key) > 0) {
            cmdNames[j + 1] = cmdNames[j];
            j--;
        }
        cmdNames[j + 1] = key;
    }
}

static void handleGetCmds(const char *cmd, char args[][CMD_MAX_ARG_LEN], int arg_count)
{
    int offset = 0;
    if (arg_count >= 1) offset = atoi(args[0]);
    cmdsList(cmdNames, cmdNameCount, offset, cmdResponseBuf, CMD_RESPONSE_BUF_SIZE);
    DBG("GETCMDS: %s\n", cmdResponseBuf);
}

/* ─── Init ──────────────────────────────────────────────────────────────── */

void commandsInit(CommandRegistry *reg)
{
    cmdRegister(reg, "ping",      handlePing,      CMD_SCOPE_ANY, true);
    cmdRegister(reg, "discover",  handlePing,      CMD_SCOPE_BROADCAST, true, true);
    cmdRegister(reg, "blink",     handleBlink,     CMD_SCOPE_ANY, true);
    cmdRegister(reg, "echo",      handleEcho,      CMD_SCOPE_ANY, false);
    cmdRegister(reg, "getcmds",   handleGetCmds,   CMD_SCOPE_ANY, false);
    cmdRegister(reg, "getparam",  handleGetParam,  CMD_SCOPE_ANY, false);
    cmdRegister(reg, "getparams", handleGetParams, CMD_SCOPE_ANY, false);
    cmdRegister(reg, "reset",     handleReset,     CMD_SCOPE_ANY, true);
    cmdRegister(reg, "savecfg",   handleSaveCfg,   CMD_SCOPE_PRIVATE, false);
    cmdRegister(reg, "setparam",  handleSetParam,  CMD_SCOPE_PRIVATE, true);
    cmdRegister(reg, "testled",   handleTestLed,   CMD_SCOPE_ANY, true);
    buildCmdNameList(reg);
}
