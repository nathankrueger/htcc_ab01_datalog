/*
 * test_main.c — Test runner for native unit tests
 *
 * Single-compilation-unit approach: include all test files here so the
 * static test harness counters are shared across all test suites.
 *
 * Compile: make (from tests/ directory)
 * Run:     make test (from project root)
 */

#include "test_harness.h"

/* Include test suites directly (single translation unit) */
#include "test_params.c"
#include "test_sensors.c"

int main(void)
{
    run_param_tests();
    run_sensor_tests();

    TEST_SUMMARY();
    return TEST_EXIT_CODE();
}
