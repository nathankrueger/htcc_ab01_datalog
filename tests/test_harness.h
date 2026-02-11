/*
 * test_harness.h — Minimal test framework for native C unit tests
 *
 * Usage:
 *   TEST(test_name) { ... ASSERT_STR_EQ(...); ... TEST_PASS(); }
 *   int main() { RUN_TEST(test_name); TEST_SUMMARY(); return TEST_EXIT_CODE(); }
 */

#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <stdio.h>
#include <string.h>

static int _tests_run    = 0;
static int _tests_passed = 0;
static int _tests_failed = 0;

#define TEST(name) static void name(void)

#define RUN_TEST(name) do {                              \
    _tests_run++;                                        \
    printf("  %-55s ", #name);                           \
    name();                                              \
} while (0)

#define ASSERT_STR_EQ(expected, actual) do {             \
    if (strcmp((expected), (actual)) != 0) {             \
        printf("FAIL\n    expected: \"%s\"\n"            \
               "    actual:   \"%s\"\n",                 \
               (expected), (actual));                    \
        _tests_failed++;                                 \
        return;                                          \
    }                                                    \
} while (0)

#define ASSERT_INT_EQ(expected, actual) do {              \
    if ((expected) != (actual)) {                         \
        printf("FAIL\n    expected: %d\n"                 \
               "    actual:   %d\n",                      \
               (int)(expected), (int)(actual));           \
        _tests_failed++;                                  \
        return;                                           \
    }                                                     \
} while (0)

#define ASSERT_TRUE(cond) do {                            \
    if (!(cond)) {                                        \
        printf("FAIL\n    condition false: %s\n", #cond); \
        _tests_failed++;                                  \
        return;                                           \
    }                                                     \
} while (0)

#define TEST_PASS() do {     \
    printf("OK\n");          \
    _tests_passed++;         \
} while (0)

#define TEST_SUMMARY() do {                              \
    printf("\n%d tests, %d passed, %d failed\n",         \
           _tests_run, _tests_passed, _tests_failed);    \
} while (0)

#define TEST_EXIT_CODE() (_tests_failed > 0 ? 1 : 0)

#endif /* TEST_HARNESS_H */
