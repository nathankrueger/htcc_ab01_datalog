#include "Arduino.h"
#include "LoRaWan_APP.h"
#include "hw.h"
#include <Adafruit_BME280.h>

/* ─── Configuration ──────────────────────────────────────────────────────── */

#ifndef NODE_ID
#define NODE_ID                  "ab01"
#endif

#define SEND_INTERVAL_MS         5000      /* 5 s between sensor reads */

#define RF_FREQUENCY             915000000  /* Hz */
#define TX_OUTPUT_POWER          14         /* dBm */
#define LORA_BANDWIDTH           0          /* 0 = 125 kHz */
#define LORA_SPREADING_FACTOR    7          /* SF7 */
#define LORA_CODINGRATE          1          /* 4/5 */
#define LORA_PREAMBLE_LENGTH     8
#define LORA_SYMBOL_TIMEOUT      0
#define LORA_FIX_LENGTH_PAYLOAD_ON  false
#define LORA_IQ_INVERSION_ON     false

/* Must match LORA_MAX_PAYLOAD in utils/protocol.py */
#define LORA_MAX_PAYLOAD         250

/*
 * Sensor-class IDs are assigned by alphabetical sort of the Python class names
 * at import time in sensors/__init__.py.  Current registry:
 *   0 = BME280TempPressureHumidity
 *   1 = MMA8452Accelerometer
 * If you add a new sensor class on the Python side, re-derive these.
 */
#define SENSOR_ID_BME280         0

/* ─── Types ──────────────────────────────────────────────────────────────── */

typedef struct {
    const char *name;   /* reading name, e.g. "Temperature" */
    int         sid;    /* sensor class ID from the Python registry */
    const char *units;  /* units as a JSON string value — non-ASCII must use
                           \uXXXX escapes to match Python ensure_ascii=True */
    float       value;
} Reading;

/* ─── CRC-32 ─────────────────────────────────────────────────────────────── */

/*
 * Standard CRC-32 (ISO 3309 / ITU-T V.42) — identical to Python zlib.crc32().
 * Bit-reversed polynomial 0xEDB88320.  Table lives in flash on ARM.
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

static uint32_t crc32_compute(const char *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = CRC32_TABLE[(crc ^ (uint8_t)data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

/* ─── Packet Builder ─────────────────────────────────────────────────────── */

/*
 * Format a float for JSON, matching Python's json.dumps round-trip output.
 *
 * We use %g (6 significant digits, no trailing zeros) but CubeCell's snprintf
 * is broken: it keeps trailing zeros (e.g. 74.1 → "74.1000").  Strip them
 * manually so the CRC string matches what Python produces after json.loads →
 * json.dumps.
 */
static int fmtVal(char *buf, size_t cap, float val)
{
    int len = snprintf(buf, cap, "%g", (double)val);

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
 * the "c" field absent — exactly what Python's
 *     json.dumps(msg, sort_keys=True, separators=(",",":"))
 * produces after stripping "c".  Key sort orders:
 *   top-level:  n  <  r  <  t
 *   per-reading: k  <  s  <  u  <  v
 *
 * Returns the byte length written to *buf, or 0 on overflow.
 */
static int buildPacket(char *buf, size_t bufCap,
                       const char *nodeId, uint32_t ts,
                       const Reading *readings, int count)
{
    /* 1. Serialise the readings array (keys already in sorted order) */
    char rBuf[220];
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

    /* 4. Final packet: same body + ,"c":"<8-hex-digits>" before closing }
     *
     * ASR650x TX-FIFO workaround:  the integrated radio's buffer-base
     * address drifts after every TX and is never re-initialised (only
     * RadioInit resets it).  This causes the first 3 bytes of every
     * payload to be silently dropped.  Prepend 4 ASCII spaces as
     * throwaway padding: json.loads on the gateway ignores leading
     * whitespace, and the CRC was computed on the JSON body only.
     */
    buf[0] = buf[1] = buf[2] = buf[3] = ' ';
    int pLen = snprintf(buf + 4, bufCap - 4,
                        "{\"n\":\"%s\",\"r\":%s,\"t\":%u,\"c\":\"%08x\"}",
                        nodeId, rBuf, ts, crc);
    if (pLen <= 0 || pLen >= (int)(bufCap - 4)) return 0;
    return pLen + 4;
}

/* ─── Globals ────────────────────────────────────────────────────────────── */

static RadioEvents_t radioEvents;
static Adafruit_BME280 bme;
static bool bmeOk = false;

static float c_to_f(float c) { return c * 9.0f / 5.0f + 32.0f; }

/* ─── setup / loop ───────────────────────────────────────────────────────── */

void setup(void)
{
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW);          /* power on peripherals */

    Serial.begin(115200);
    boardInitMcu();

    /* BME280 — try both common I2C addresses */
    bmeOk = bme.begin(0x76);
    if (!bmeOk) bmeOk = bme.begin(0x77);
    if (!bmeOk) Serial.println("ERROR: BME280 not found on 0x76 or 0x77");

    /* LoRa */
    Radio.Init(&radioEvents);
    Radio.SetChannel(RF_FREQUENCY);
    Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                      LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                      LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                      true, 0, 0, LORA_IQ_INVERSION_ON, 3000);
}

void loop(void)
{
    if (!bmeOk) {
        Serial.println("BME280 not available — retrying...");
        bmeOk = bme.begin(0x76);
        if (!bmeOk) bmeOk = bme.begin(0x77);
        delay(SEND_INTERVAL_MS);
        return;
    }

    /* ── Read sensor ── */
    float tempF   = c_to_f(bme.readTemperature());   /* °C → °F */
    float pressure = bme.readPressure() / 100.0f;    /* Pa  → hPa */
    float humidity = bme.readHumidity();

    /* Guard against NaN / Inf from a bad read — %g would produce non-JSON */
    if (isnan(tempF) || isinf(tempF) ||
        isnan(pressure) || isinf(pressure) ||
        isnan(humidity) || isinf(humidity)) {
        Serial.println("ERROR: BME280 returned NaN/Inf, skipping");
        delay(SEND_INTERVAL_MS);
        return;
    }

    /* Round to match BME280 output precision */
    tempF     = roundf(tempF     * 10.0f)  / 10.0f;   /* 1 dp */
    pressure  = roundf(pressure  * 100.0f) / 100.0f;  /* 2 dp */
    humidity  = roundf(humidity  * 10.0f)  / 10.0f;   /* 1 dp */

    Serial.printf("T=%.1f F  P=%.2f hPa  H=%.1f %%\n",
                  tempF, pressure, humidity);

    /*
     * Reading descriptors.
     *
     * Units must use JSON \uXXXX escapes for any non-ASCII characters so the
     * CRC matches Python's json.dumps(..., ensure_ascii=True).
     * The degree sign ° (U+00B0) becomes \u00b0 in the JSON wire bytes.
     * In this C string literal the backslash is escaped once: "\\u00b0F".
     */
    Reading readings[] = {
        { "Temperature", SENSOR_ID_BME280, "\\u00b0F", tempF    },
        { "Pressure",    SENSOR_ID_BME280, "hPa",      pressure },
        { "Humidity",    SENSOR_ID_BME280, "%",        humidity  },
    };
    const int NUM_READINGS = 3;

    /*
     * Send with automatic packet splitting.
     *
     * All three BME280 readings fit comfortably in one packet (~173 bytes for
     * a short NODE_ID), but the loop handles the general case: it greedily
     * packs as many readings as possible into each packet while staying at or
     * below LORA_MAX_PAYLOAD.
     *
     * TODO: replace the timestamp (currently 0) with a real Unix epoch.
     *       The gateway passes it through to the dashboard unchanged, so 0
     *       will show up as 1970-01-01.  Add NTP sync or an RTC module to fix.
     */
    char pkt[LORA_MAX_PAYLOAD + 1];
    int  start = 0;

    while (start < NUM_READINGS) {
        int end  = NUM_READINGS;
        int pLen = 0;

        /* shrink the window until the packet fits */
        while (end > start) {
            pLen = buildPacket(pkt, sizeof(pkt),
                               NODE_ID, 0u,
                               &readings[start], end - start);
            if (pLen > 0 && pLen <= LORA_MAX_PAYLOAD) break;
            end--;
        }

        if (end == start) {
            Serial.printf("ERROR: reading \"%s\" alone exceeds max payload\n",
                          readings[start].name);
            start++;
            continue;
        }

        Radio.Send((uint8_t *)pkt, pLen);
        Serial.printf("Sent %d/%d readings [%d bytes]: %.*s\n",
                      end - start, NUM_READINGS, pLen, pLen, pkt);

        start = end;
        if (start < NUM_READINGS)
            delay(100);   /* brief gap between split packets */
    }

    delay(SEND_INTERVAL_MS);
}
