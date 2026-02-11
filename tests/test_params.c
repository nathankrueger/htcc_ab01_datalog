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
static uint16_t   testNodeVersion;
static uint8_t    testBW;
static uint8_t    testRxDuty;
static uint8_t    testSF;
static int8_t     testTxPwr;

/* Track onSet callback invocations */
static int onSetCallCount = 0;
static void testOnSetTxPwr(const char *name) { (void)name; onSetCallCount++; }
static void testOnSetRadio(const char *name) { (void)name; onSetCallCount++; }

/* Standard param table (alpha-sorted by name) */
static const ParamDef testTable[] = {
    { "bw",      PARAM_UINT8,  &testBW,               0,   2, true,  testOnSetRadio,  offsetof(NodeConfig, bandwidth)        },
    { "nodeid",  PARAM_STRING, testCfg.nodeId,         0,   0, false, NULL,            CFG_OFFSET_NONE                        },
    { "nodev",   PARAM_UINT16, &testNodeVersion,        0,   0, false, NULL,            CFG_OFFSET_NONE                        },
    { "rxduty",  PARAM_UINT8,  &testRxDuty,            0, 100, true,  NULL,            offsetof(NodeConfig, rxDutyPercent)     },
    { "sf",      PARAM_UINT8,  &testSF,                7,  12, true,  testOnSetRadio,  offsetof(NodeConfig, spreadingFactor)   },
    { "txpwr",   PARAM_INT8,   &testTxPwr,           -17,  22, true,  testOnSetTxPwr,  offsetof(NodeConfig, txOutputPower)     },
};
#define TEST_TABLE_COUNT (sizeof(testTable) / sizeof(testTable[0]))

static void resetFixtures(void)
{
    memset(&testCfg, 0, sizeof(testCfg));
    strncpy(testCfg.nodeId, "ab01", sizeof(testCfg.nodeId));
    testNodeVersion = 1;
    testCfg.txOutputPower = 14;
    testCfg.rxDutyPercent = 90;
    testCfg.spreadingFactor = 7;
    testCfg.bandwidth = 0;
    testBW = 0;
    testRxDuty = 90;
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
    ASSERT_STR_EQ("ab01", testCfg.nodeId);  /* unchanged */
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
    ASSERT_STR_EQ("{\"m\":0,\"p\":{\"bw\":0,\"nodeid\":\"ab01\",\"nodev\":1,\"rxduty\":90,\"sf\":7,\"txpwr\":14}}", buf);
    TEST_PASS();
}

TEST(test_paramsList_pagination)
{
    resetFixtures();
    /* Buffer too small for all 6 params — forces pagination */
    char buf[50];
    int n = paramsList(testTable, TEST_TABLE_COUNT, 0, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    /* Should have "m":1 indicating more pages */
    ASSERT_TRUE(strstr(buf, "\"m\":1") != NULL);
    /* Should contain the first params but not all */
    ASSERT_TRUE(strstr(buf, "\"nodeid\"") != NULL);
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
    ASSERT_STR_EQ("{\"m\":0,\"p\":{\"sf\":7,\"txpwr\":14}}", buf);
    TEST_PASS();
}

TEST(test_paramsList_alpha_order)
{
    resetFixtures();
    char buf[256];
    paramsList(testTable, TEST_TABLE_COUNT, 0, buf, sizeof(buf));
    /* Verify keys appear in alphabetical order: bw < nodeid < nodev < rxduty < sf < txpwr */
    const char *bw     = strstr(buf, "\"bw\"");
    const char *nodeid = strstr(buf, "\"nodeid\"");
    const char *nodev  = strstr(buf, "\"nodev\"");
    const char *rxduty = strstr(buf, "\"rxduty\"");
    const char *sf     = strstr(buf, "\"sf\"");
    const char *txpwr  = strstr(buf, "\"txpwr\"");
    ASSERT_TRUE(bw != NULL);
    ASSERT_TRUE(nodeid != NULL);
    ASSERT_TRUE(nodev != NULL);
    ASSERT_TRUE(rxduty != NULL);
    ASSERT_TRUE(sf != NULL);
    ASSERT_TRUE(txpwr != NULL);
    ASSERT_TRUE(bw < nodeid);
    ASSERT_TRUE(nodeid < nodev);
    ASSERT_TRUE(nodev < rxduty);
    ASSERT_TRUE(rxduty < sf);
    ASSERT_TRUE(sf < txpwr);
    TEST_PASS();
}

/* ─── cmdsList Tests ─────────────────────────────────────────────────────── */

static const char *testCmdNames[] = {
    "blink", "discover", "echo", "getcmds", "getparam",
    "getparams", "ping", "reset", "savecfg", "setparam", "testled"
};
#define TEST_CMD_COUNT (sizeof(testCmdNames) / sizeof(testCmdNames[0]))

TEST(test_cmdsList_all_fit)
{
    char buf[256];
    int n = cmdsList(testCmdNames, TEST_CMD_COUNT, 0, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ("{\"c\":[\"blink\",\"discover\",\"echo\",\"getcmds\",\"getparam\","
                  "\"getparams\",\"ping\",\"reset\",\"savecfg\",\"setparam\",\"testled\"],\"m\":0}", buf);
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
    int n = cmdsList(testCmdNames, TEST_CMD_COUNT, 9, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ("{\"c\":[\"setparam\",\"testled\"],\"m\":0}", buf);
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
    testTxPwr = -5;
    testSF = 12;
    testBW = 2;

    NodeConfig dst;
    memset(&dst, 0, sizeof(dst));

    paramsSyncToConfig(testTable, TEST_TABLE_COUNT, &dst);
    ASSERT_INT_EQ(42, dst.rxDutyPercent);
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

    /* Read-only fields (magic, cfgVersion, nodeId) should NOT have been touched */
    ASSERT_INT_EQ(0xFF, dst.magic);
    ASSERT_INT_EQ(0xFF, dst.cfgVersion);
    ASSERT_INT_EQ(0xFF, (uint8_t)dst.nodeId[0]);
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

    /* cmdsList */
    RUN_TEST(test_cmdsList_all_fit);
    RUN_TEST(test_cmdsList_pagination);
    RUN_TEST(test_cmdsList_empty);
    RUN_TEST(test_cmdsList_offset);
    RUN_TEST(test_cmdsList_offset_past_end);

    /* paramsSyncToConfig */
    RUN_TEST(test_syncToConfig_copies_writable);
    RUN_TEST(test_syncToConfig_skips_readonly);
}
