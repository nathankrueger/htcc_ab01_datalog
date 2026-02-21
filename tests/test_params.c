/*
 * test_params.c — Unit tests for shared/params.h
 *
 * Compiled natively with gcc — no Arduino dependencies.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "config_types.h"
#include "params.h"
#include "test_harness.h"

/* ─── Test Fixtures ──────────────────────────────────────────────────────── */

/* Simulated runtime globals */
static NodeConfig testCfg;
static char       testNodeId[16];
static uint16_t   testNodeVersion;
static uint8_t    testBW;
static uint8_t    testRxDuty;
static uint16_t   testBme280RateSec;
static uint8_t    testSF;
static int8_t     testTxPwr;

/* Track onSet callback invocations */
static int onSetCallCount = 0;
static void testOnSetTxPwr(const char *name) { (void)name; onSetCallCount++; }
static void testOnSetRadio(const char *name) { (void)name; onSetCallCount++; }

/* Standard param table (alpha-sorted by name) */
static const ParamDef testTable[] = {
    { "bme280_rate",     PARAM_UINT16, &testBme280RateSec,  NULL,  1, 32767, true,  NULL,            offsetof(NodeConfig, bme280RateSec)    },
    { "bw",              PARAM_UINT8,  &testBW,             NULL,  0,    2, true,  testOnSetRadio,  offsetof(NodeConfig, bandwidth)        },
    { "nodeid",          PARAM_STRING, testNodeId,          NULL,  0,    0, false, NULL,            CFG_OFFSET_NONE                        },
    { "nodev",           PARAM_UINT16, &testNodeVersion,    NULL,  0,    0, false, NULL,            CFG_OFFSET_NONE                        },
    { "rxduty",          PARAM_UINT8,  &testRxDuty,         NULL,  0,  100, true,  NULL,            offsetof(NodeConfig, rxDutyPercent)     },
    { "sf",              PARAM_UINT8,  &testSF,             NULL,  7,   12, true,  testOnSetRadio,  offsetof(NodeConfig, spreadingFactor)   },
    { "txpwr",           PARAM_INT8,   &testTxPwr,          NULL, -17,  22, true,  testOnSetTxPwr,  offsetof(NodeConfig, txOutputPower)     },
};
#define TEST_TABLE_COUNT (sizeof(testTable) / sizeof(testTable[0]))

static void resetFixtures(void)
{
    memset(&testCfg, 0, sizeof(testCfg));
    memset(testNodeId, 0, sizeof(testNodeId));
    strncpy(testNodeId, "ab01", sizeof(testNodeId) - 1);
    testNodeVersion = 1;
    testCfg.txOutputPower = 14;
    testCfg.rxDutyPercent = 90;
    testCfg.spreadingFactor = 7;
    testCfg.bandwidth = 0;
    testBW = 0;
    testRxDuty = 90;
    testBme280RateSec = 5;
    testCfg.bme280RateSec = 5;
    testSF = 7;
    testTxPwr = 14;
    onSetCallCount = 0;
}

/* ─── paramGet Tests ─────────────────────────────────────────────────────── */

TEST(test_paramGet_int8)
{
    resetFixtures();
    char buf[128];
    paramGet(testTable, TEST_TABLE_COUNT, "txpwr", buf, sizeof(buf));
    ASSERT_STR_EQ("{\"txpwr\":14}", buf);
    TEST_PASS();
}

TEST(test_paramGet_uint8)
{
    resetFixtures();
    char buf[128];
    paramGet(testTable, TEST_TABLE_COUNT, "rxduty", buf, sizeof(buf));
    ASSERT_STR_EQ("{\"rxduty\":90}", buf);
    TEST_PASS();
}

TEST(test_paramGet_uint16)
{
    resetFixtures();
    char buf[128];
    paramGet(testTable, TEST_TABLE_COUNT, "nodev", buf, sizeof(buf));
    ASSERT_STR_EQ("{\"nodev\":1}", buf);
    TEST_PASS();
}

TEST(test_paramGet_string)
{
    resetFixtures();
    char buf[128];
    paramGet(testTable, TEST_TABLE_COUNT, "nodeid", buf, sizeof(buf));
    ASSERT_STR_EQ("{\"nodeid\":\"ab01\"}", buf);
    TEST_PASS();
}

TEST(test_paramGet_unknown)
{
    resetFixtures();
    char buf[128];
    paramGet(testTable, TEST_TABLE_COUNT, "bogus", buf, sizeof(buf));
    ASSERT_STR_EQ("{\"e\":\"unknown param\"}", buf);
    TEST_PASS();
}

TEST(test_paramGet_negative_value)
{
    resetFixtures();
    testTxPwr = -10;
    char buf[128];
    paramGet(testTable, TEST_TABLE_COUNT, "txpwr", buf, sizeof(buf));
    ASSERT_STR_EQ("{\"txpwr\":-10}", buf);
    TEST_PASS();
}

/* ─── paramSet Tests ─────────────────────────────────────────────────────── */

TEST(test_paramSet_valid)
{
    resetFixtures();
    char buf[128];
    paramSet(testTable, TEST_TABLE_COUNT, "rxduty", "50", buf, sizeof(buf));
    ASSERT_STR_EQ("{\"rxduty\":50}", buf);
    ASSERT_INT_EQ(50, testRxDuty);
    TEST_PASS();
}

TEST(test_paramSet_range_low)
{
    resetFixtures();
    char buf[128];
    paramSet(testTable, TEST_TABLE_COUNT, "txpwr", "-20", buf, sizeof(buf));
    ASSERT_STR_EQ("{\"e\":\"range: -17..22\"}", buf);
    ASSERT_INT_EQ(14, testTxPwr);  /* unchanged */
    TEST_PASS();
}

TEST(test_paramSet_range_high)
{
    resetFixtures();
    char buf[128];
    paramSet(testTable, TEST_TABLE_COUNT, "rxduty", "101", buf, sizeof(buf));
    ASSERT_STR_EQ("{\"e\":\"range: 0..100\"}", buf);
    ASSERT_INT_EQ(90, testRxDuty);  /* unchanged */
    TEST_PASS();
}

TEST(test_paramSet_readonly)
{
    resetFixtures();
    char buf[128];
    paramSet(testTable, TEST_TABLE_COUNT, "nodeid", "xx", buf, sizeof(buf));
    ASSERT_STR_EQ("{\"e\":\"read-only: nodeid\"}", buf);
    ASSERT_STR_EQ("ab01", testNodeId);  /* unchanged */
    TEST_PASS();
}

TEST(test_paramSet_unknown)
{
    resetFixtures();
    char buf[128];
    paramSet(testTable, TEST_TABLE_COUNT, "bogus", "1", buf, sizeof(buf));
    ASSERT_STR_EQ("{\"e\":\"unknown param\"}", buf);
    TEST_PASS();
}

TEST(test_paramSet_callback)
{
    resetFixtures();
    ASSERT_INT_EQ(0, onSetCallCount);
    char buf[128];
    paramSet(testTable, TEST_TABLE_COUNT, "txpwr", "20", buf, sizeof(buf));
    ASSERT_INT_EQ(1, onSetCallCount);
    ASSERT_INT_EQ(20, testTxPwr);
    TEST_PASS();
}

TEST(test_paramSet_boundary_min)
{
    resetFixtures();
    char buf[128];
    paramSet(testTable, TEST_TABLE_COUNT, "txpwr", "-17", buf, sizeof(buf));
    ASSERT_STR_EQ("{\"txpwr\":-17}", buf);
    ASSERT_INT_EQ(-17, testTxPwr);
    TEST_PASS();
}

TEST(test_paramSet_boundary_max)
{
    resetFixtures();
    char buf[128];
    paramSet(testTable, TEST_TABLE_COUNT, "rxduty", "100", buf, sizeof(buf));
    ASSERT_STR_EQ("{\"rxduty\":100}", buf);
    ASSERT_INT_EQ(100, testRxDuty);
    TEST_PASS();
}

/* ─── paramsList Tests ───────────────────────────────────────────────────── */

TEST(test_paramsList_all_fit)
{
    resetFixtures();
    char buf[256];
    int n = paramsList(testTable, TEST_TABLE_COUNT, 0, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ("{\"m\":0,\"p\":{\"bme280_rate\":5,\"bw\":0,\"nodeid\":\"ab01\",\"nodev\":1,\"rxduty\":90,\"sf\":7,\"txpwr\":14}}", buf);
    TEST_PASS();
}

TEST(test_paramsList_pagination)
{
    resetFixtures();
    /* Buffer too small for all 7 params — forces pagination */
    char buf[50];
    int n = paramsList(testTable, TEST_TABLE_COUNT, 0, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    /* Should have "m":1 indicating more pages */
    ASSERT_TRUE(strstr(buf, "\"m\":1") != NULL);
    /* Should contain the first param but not all */
    ASSERT_TRUE(strstr(buf, "\"bme280_rate\"") != NULL);
    /* Should be valid JSON (starts with { ends with }) */
    ASSERT_TRUE(buf[0] == '{');
    ASSERT_TRUE(buf[n - 1] == '}');
    TEST_PASS();
}

TEST(test_paramsList_empty)
{
    char buf[128];
    int n = paramsList(testTable, 0, 0, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ("{\"m\":0,\"p\":{}}", buf);
    TEST_PASS();
}

TEST(test_paramsList_offset_past_end)
{
    resetFixtures();
    char buf[128];
    int n = paramsList(testTable, TEST_TABLE_COUNT, 100, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ("{\"m\":0,\"p\":{}}", buf);
    TEST_PASS();
}

TEST(test_paramsList_offset_mid)
{
    resetFixtures();
    char buf[256];
    int n = paramsList(testTable, TEST_TABLE_COUNT, 4, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ("{\"m\":0,\"p\":{\"rxduty\":90,\"sf\":7,\"txpwr\":14}}", buf);
    TEST_PASS();
}

TEST(test_paramsList_alpha_order)
{
    resetFixtures();
    char buf[256];
    paramsList(testTable, TEST_TABLE_COUNT, 0, buf, sizeof(buf));
    /* Verify keys appear in alphabetical order */
    const char *bme280_rate     = strstr(buf, "\"bme280_rate\"");
    const char *bw              = strstr(buf, "\"bw\"");
    const char *nodeid          = strstr(buf, "\"nodeid\"");
    const char *nodev           = strstr(buf, "\"nodev\"");
    const char *rxduty          = strstr(buf, "\"rxduty\"");
    const char *sf              = strstr(buf, "\"sf\"");
    const char *txpwr           = strstr(buf, "\"txpwr\"");
    ASSERT_TRUE(bme280_rate != NULL);
    ASSERT_TRUE(bw != NULL);
    ASSERT_TRUE(nodeid != NULL);
    ASSERT_TRUE(nodev != NULL);
    ASSERT_TRUE(rxduty != NULL);
    ASSERT_TRUE(sf != NULL);
    ASSERT_TRUE(txpwr != NULL);
    ASSERT_TRUE(bme280_rate < bw);
    ASSERT_TRUE(bw < nodeid);
    ASSERT_TRUE(nodeid < nodev);
    ASSERT_TRUE(nodev < rxduty);
    ASSERT_TRUE(rxduty < sf);
    ASSERT_TRUE(sf < txpwr);
    TEST_PASS();
}

/* ─── paramsTableIsSorted Tests ──────────────────────────────────────────── */

TEST(test_paramsTableIsSorted_valid)
{
    ASSERT_TRUE(paramsTableIsSorted(testTable, TEST_TABLE_COUNT));
    TEST_PASS();
}

TEST(test_paramsTableIsSorted_unsorted)
{
    static const ParamDef unsorted[] = {
        { "bw",    PARAM_UINT8, NULL, NULL, 0, 0, false, NULL, CFG_OFFSET_NONE },
        { "txpwr", PARAM_INT8,  NULL, NULL, 0, 0, false, NULL, CFG_OFFSET_NONE },
        { "sf",    PARAM_UINT8, NULL, NULL, 0, 0, false, NULL, CFG_OFFSET_NONE },
    };
    ASSERT_TRUE(!paramsTableIsSorted(unsorted, 3));
    TEST_PASS();
}

TEST(test_paramsTableIsSorted_duplicates)
{
    static const ParamDef dupes[] = {
        { "bw", PARAM_UINT8, NULL, NULL, 0, 0, false, NULL, CFG_OFFSET_NONE },
        { "bw", PARAM_UINT8, NULL, NULL, 0, 0, false, NULL, CFG_OFFSET_NONE },
    };
    ASSERT_TRUE(!paramsTableIsSorted(dupes, 2));
    TEST_PASS();
}

TEST(test_paramsTableIsSorted_single)
{
    static const ParamDef single[] = {
        { "x", PARAM_UINT8, NULL, NULL, 0, 0, false, NULL, CFG_OFFSET_NONE },
    };
    ASSERT_TRUE(paramsTableIsSorted(single, 1));
    TEST_PASS();
}

TEST(test_paramsTableIsSorted_empty)
{
    ASSERT_TRUE(paramsTableIsSorted(NULL, 0));
    TEST_PASS();
}

/* ─── cmdsList Tests ─────────────────────────────────────────────────────── */

static const char *testCmdNames[] = {
    "batt", "blink", "discover", "echo", "getcmds", "getparam",
    "getparams", "ping", "rcfg_radio", "reset", "rssi",
    "savecfg", "setparam", "testled", "uptime"
};
#define TEST_CMD_COUNT (sizeof(testCmdNames) / sizeof(testCmdNames[0]))

TEST(test_cmdsList_all_fit)
{
    char buf[256];
    int n = cmdsList(testCmdNames, TEST_CMD_COUNT, 0, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ("{\"c\":[\"batt\",\"blink\",\"discover\",\"echo\",\"getcmds\",\"getparam\","
                  "\"getparams\",\"ping\",\"rcfg_radio\",\"reset\",\"rssi\","
                  "\"savecfg\",\"setparam\",\"testled\",\"uptime\"],\"m\":0}", buf);
    TEST_PASS();
}

TEST(test_cmdsList_pagination)
{
    char buf[60];
    int n = cmdsList(testCmdNames, TEST_CMD_COUNT, 0, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    /* Should have "m":1 indicating more pages */
    ASSERT_TRUE(strstr(buf, "\"m\":1") != NULL);
    ASSERT_TRUE(buf[0] == '{');
    ASSERT_TRUE(buf[n - 1] == '}');
    TEST_PASS();
}

TEST(test_cmdsList_empty)
{
    char buf[128];
    int n = cmdsList(testCmdNames, 0, 0, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ("{\"c\":[],\"m\":0}", buf);
    TEST_PASS();
}

TEST(test_cmdsList_offset)
{
    char buf[256];
    int n = cmdsList(testCmdNames, TEST_CMD_COUNT, 11, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ("{\"c\":[\"savecfg\",\"setparam\",\"testled\",\"uptime\"],\"m\":0}", buf);
    TEST_PASS();
}

TEST(test_cmdsList_offset_past_end)
{
    char buf[128];
    int n = cmdsList(testCmdNames, TEST_CMD_COUNT, 100, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ("{\"c\":[],\"m\":0}", buf);
    TEST_PASS();
}

/* ─── paramsSyncToConfig Tests ───────────────────────────────────────────── */

TEST(test_syncToConfig_copies_writable)
{
    resetFixtures();
    testRxDuty = 42;
    testBme280RateSec = 30;
    testTxPwr = -5;
    testSF = 12;
    testBW = 2;

    NodeConfig dst;
    memset(&dst, 0, sizeof(dst));

    paramsSyncToConfig(testTable, TEST_TABLE_COUNT, &dst);
    ASSERT_INT_EQ(42, dst.rxDutyPercent);
    ASSERT_INT_EQ(30, dst.bme280RateSec);
    ASSERT_INT_EQ(-5, dst.txOutputPower);
    ASSERT_INT_EQ(12, dst.spreadingFactor);
    ASSERT_INT_EQ(2, dst.bandwidth);
    TEST_PASS();
}

TEST(test_syncToConfig_skips_readonly)
{
    resetFixtures();
    NodeConfig dst;
    memset(&dst, 0xFF, sizeof(dst));  /* fill with 0xFF to detect writes */

    paramsSyncToConfig(testTable, TEST_TABLE_COUNT, &dst);

    /* Writable fields should be set */
    ASSERT_INT_EQ(90, dst.rxDutyPercent);
    ASSERT_INT_EQ(14, dst.txOutputPower);
    ASSERT_INT_EQ(7, dst.spreadingFactor);
    ASSERT_INT_EQ(0, dst.bandwidth);

    /* Read-only fields (magic, cfgVersion) should NOT have been touched */
    ASSERT_INT_EQ(0xFF, dst.magic);
    ASSERT_INT_EQ(0xFF, dst.cfgVersion);
    TEST_PASS();
}

/* ─── paramsApplyStaged Tests ────────────────────────────────────────────── */

/* Separate table with runtimePtr set for staged-param testing */
static uint8_t stagedBW = 0;
static uint8_t stagedSF = 7;
static int8_t  stagedTxPwr = 14;
static NodeConfig stagedCfg;

static const ParamDef stagedTable[] = {
    { "bw",    PARAM_UINT8, &stagedCfg.bandwidth,        &stagedBW,    0,   2, true, NULL, offsetof(NodeConfig, bandwidth)       },
    { "rxduty", PARAM_UINT8, &stagedBW,                  NULL,         0, 100, true, NULL, offsetof(NodeConfig, rxDutyPercent)    },
    { "sf",    PARAM_UINT8, &stagedCfg.spreadingFactor,  &stagedSF,    7,  12, true, NULL, offsetof(NodeConfig, spreadingFactor)  },
    { "txpwr", PARAM_INT8,  &stagedCfg.txOutputPower,    &stagedTxPwr, -17, 22, true, NULL, offsetof(NodeConfig, txOutputPower)   },
};
#define STAGED_TABLE_COUNT (sizeof(stagedTable) / sizeof(stagedTable[0]))

TEST(test_paramsApplyStaged)
{
    /* Set cfg fields to new values (simulating setparam on staged params) */
    memset(&stagedCfg, 0, sizeof(stagedCfg));
    stagedCfg.bandwidth = 2;
    stagedCfg.spreadingFactor = 10;
    stagedCfg.txOutputPower = -5;

    /* Runtime globals still at old values */
    stagedBW = 0;
    stagedSF = 7;
    stagedTxPwr = 14;

    /* Apply staged params */
    paramsApplyStaged(stagedTable, STAGED_TABLE_COUNT);

    /* Staged params (runtimePtr != NULL) should be updated */
    ASSERT_INT_EQ(2, stagedBW);
    ASSERT_INT_EQ(10, stagedSF);
    ASSERT_INT_EQ(-5, stagedTxPwr);
    TEST_PASS();
}

TEST(test_paramsApplyStaged_skips_immediate)
{
    /* rxduty in stagedTable has runtimePtr = NULL (immediate param) */
    memset(&stagedCfg, 0, sizeof(stagedCfg));
    stagedBW = 42;  /* rxduty entry's ptr points here */

    paramsApplyStaged(stagedTable, STAGED_TABLE_COUNT);

    /* stagedBW should NOT be overwritten by paramsApplyStaged for the
     * rxduty entry (runtimePtr is NULL) — but it IS the runtimePtr target
     * for the bw entry. bw's ptr is &stagedCfg.bandwidth which is 0. */
    ASSERT_INT_EQ(0, stagedBW);  /* overwritten by bw's staged apply */
    TEST_PASS();
}

/* ─── Test Runner ────────────────────────────────────────────────────────── */

void run_param_tests(void)
{
    printf("params.h tests:\n");

    /* paramGet */
    RUN_TEST(test_paramGet_int8);
    RUN_TEST(test_paramGet_uint8);
    RUN_TEST(test_paramGet_uint16);
    RUN_TEST(test_paramGet_string);
    RUN_TEST(test_paramGet_unknown);
    RUN_TEST(test_paramGet_negative_value);

    /* paramSet */
    RUN_TEST(test_paramSet_valid);
    RUN_TEST(test_paramSet_range_low);
    RUN_TEST(test_paramSet_range_high);
    RUN_TEST(test_paramSet_readonly);
    RUN_TEST(test_paramSet_unknown);
    RUN_TEST(test_paramSet_callback);
    RUN_TEST(test_paramSet_boundary_min);
    RUN_TEST(test_paramSet_boundary_max);

    /* paramsList */
    RUN_TEST(test_paramsList_all_fit);
    RUN_TEST(test_paramsList_pagination);
    RUN_TEST(test_paramsList_empty);
    RUN_TEST(test_paramsList_offset_past_end);
    RUN_TEST(test_paramsList_offset_mid);
    RUN_TEST(test_paramsList_alpha_order);

    /* paramsTableIsSorted */
    RUN_TEST(test_paramsTableIsSorted_valid);
    RUN_TEST(test_paramsTableIsSorted_unsorted);
    RUN_TEST(test_paramsTableIsSorted_duplicates);
    RUN_TEST(test_paramsTableIsSorted_single);
    RUN_TEST(test_paramsTableIsSorted_empty);

    /* cmdsList */
    RUN_TEST(test_cmdsList_all_fit);
    RUN_TEST(test_cmdsList_pagination);
    RUN_TEST(test_cmdsList_empty);
    RUN_TEST(test_cmdsList_offset);
    RUN_TEST(test_cmdsList_offset_past_end);

    /* paramsSyncToConfig */
    RUN_TEST(test_syncToConfig_copies_writable);
    RUN_TEST(test_syncToConfig_skips_readonly);

    /* paramsApplyStaged */
    RUN_TEST(test_paramsApplyStaged);
    RUN_TEST(test_paramsApplyStaged_skips_immediate);
}
