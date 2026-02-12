/*
 * commands.h — Command handler declarations for data_log sketch
 *
 * All command handlers, the parameter table, and the command name list
 * live in commands.cpp.  data_log.ino calls commandsInit() once from
 * setup() to register everything.
 */

#ifndef COMMANDS_H
#define COMMANDS_H

#include "packets.h"
#include "config_types.h"

/* ─── Shared response buffer (defined in commands.cpp) ───────────────── */

extern char cmdResponseBuf[CMD_RESPONSE_BUF_SIZE];

/* ─── Globals defined in data_log.ino, used by command handlers ──────── */

extern NodeConfig    cfg;
extern uint8_t       rxDutyPercent;
extern int8_t        txPower;
extern uint8_t       spreadFactor;
extern uint8_t       loraBW;
extern uint32_t      n2gFreqHz;
extern uint32_t      g2nFreqHz;
extern bool          blinkActive;
extern unsigned long blinkOffTime;
extern int16_t       lastRxRssi;

/* ─── Public Interface ───────────────────────────────────────────────── */

/*
 * Register all command handlers and build the sorted command name list.
 * Call once from setup() after cfgLoad() and radio init.
 */
void commandsInit(CommandRegistry *reg);

/*
 * Apply TX/RX config using current runtime params.
 * Call from setup() after loading params from EEPROM.
 */
void applyTxConfig(void);
void applyRxConfig(void);

#endif /* COMMANDS_H */
