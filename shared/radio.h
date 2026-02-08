/*
 * radio.h — Shared LoRa radio configuration for CubeCell HTCC-AB01
 *
 * Common settings for dual-channel LoRa architecture used by both
 * data_log and range_test sketches.
 */

#ifndef RADIO_H
#define RADIO_H

/* ─── Dual-Channel Frequencies ──────────────────────────────────────────────
 *
 * N2G (Node to Gateway): Sensor data broadcasts + Command ACKs
 * G2N (Gateway to Node): Command packets only
 *
 * Node listens on G2N during RX window — no sensor interference!
 */
#define RF_N2G_FREQUENCY         915000000  /* Hz - sensors + ACKs */
#define RF_G2N_FREQUENCY         915500000  /* Hz - commands */

/* ─── TX Power Limits ───────────────────────────────────────────────────────
 *
 * ASR650x valid range: -17 to 22 dBm
 * For field range testing, use MAX_TX_POWER.
 * For indoor/short-range testing, lower values save power.
 */
#define MIN_TX_POWER             (-17)      /* dBm */
#define MAX_TX_POWER             22         /* dBm */
#define DEFAULT_TX_POWER         14         /* dBm - balanced for typical use */

/* ─── LoRa Modulation Settings ──────────────────────────────────────────────
 *
 * These settings define the LoRa physical layer parameters.
 * All nodes and gateways must use identical settings.
 */
#define LORA_BANDWIDTH           0          /* 0 = 125 kHz */
#define LORA_SPREADING_FACTOR    7          /* SF7 - fast, shorter range */
#define LORA_CODINGRATE          1          /* 4/5 */
#define LORA_PREAMBLE_LENGTH     8
#define LORA_SYMBOL_TIMEOUT      0
#define LORA_FIX_LENGTH_PAYLOAD_ON  false
#define LORA_IQ_INVERSION_ON     false

/* ─── Payload Limits ────────────────────────────────────────────────────────
 *
 * Must match LORA_MAX_PAYLOAD in gateway's utils/protocol.py
 */
#define LORA_MAX_PAYLOAD         250

#endif /* RADIO_H */
