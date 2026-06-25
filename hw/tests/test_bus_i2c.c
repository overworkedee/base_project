#include "hw/bus/bus_i2c.h"
#include "hw/hw_error.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    printf("  TEST: %s ... ", name); \
} while(0)

#define PASS() do { \
    printf("PASS\n"); tests_passed++; \
} while(0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); tests_failed++; \
} while(0)

/* ── Test cases ─────────────────────────────────────────────────── */

static void test_open_null_device(void)
{
    TEST("bus_i2c_open(NULL) returns NULL");
    bus_i2c_t* bus = bus_i2c_open(NULL);
    if (bus == NULL) PASS();
    else { bus_i2c_close(bus); FAIL("expected NULL"); }
}

static void test_open_invalid_device(void)
{
    TEST("bus_i2c_open(invalid path) returns NULL");
    bus_i2c_t* bus = bus_i2c_open("/dev/i2c-nonexistent");
    if (bus == NULL) PASS();
    else { bus_i2c_close(bus); FAIL("expected NULL"); }
}

static void test_close_null(void)
{
    TEST("bus_i2c_close(NULL) does not crash");
    bus_i2c_close(NULL);
    PASS();
}

static void test_transfer_null_bus(void)
{
    TEST("bus_i2c_transfer(NULL) returns HW_ERR_PARAM");
    uint8_t buf[4] = {0};
    hw_err_t ret = bus_i2c_transfer(NULL, 0x50, buf, 1, NULL, 0);
    if (ret == HW_ERR_PARAM) PASS();
    else FAIL("expected HW_ERR_PARAM");
}

static void test_write_null_bus(void)
{
    TEST("bus_i2c_write(NULL) returns HW_ERR_PARAM");
    uint8_t buf = 0;
    hw_err_t ret = bus_i2c_write(NULL, 0x50, &buf, 1);
    if (ret == HW_ERR_PARAM) PASS();
    else FAIL("expected HW_ERR_PARAM");
}

static void test_read_null_bus(void)
{
    TEST("bus_i2c_read(NULL) returns HW_ERR_PARAM");
    uint8_t buf = 0;
    hw_err_t ret = bus_i2c_read(NULL, 0x50, &buf, 1);
    if (ret == HW_ERR_PARAM) PASS();
    else FAIL("expected HW_ERR_PARAM");
}

static void test_transfer_null_tx_buf(void)
{
    TEST("bus_i2c_transfer(tx_len>0, tx=NULL) returns HW_ERR_PARAM");
    bus_i2c_t* bus = bus_i2c_open("/dev/i2c-1");
    if (!bus) { printf("SKIP (no /dev/i2c-1)\n"); return; }
    hw_err_t ret = bus_i2c_transfer(bus, 0x50, NULL, 4, NULL, 0);
    if (ret == HW_ERR_PARAM) PASS();
    else FAIL("expected HW_ERR_PARAM");
    bus_i2c_close(bus);
}

static void test_transfer_null_rx_buf(void)
{
    TEST("bus_i2c_transfer(rx_len>0, rx=NULL) returns HW_ERR_PARAM");
    bus_i2c_t* bus = bus_i2c_open("/dev/i2c-1");
    if (!bus) { printf("SKIP (no /dev/i2c-1)\n"); return; }
    uint8_t tx = 0x00;
    hw_err_t ret = bus_i2c_transfer(bus, 0x50, &tx, 1, NULL, 4);
    if (ret == HW_ERR_PARAM) PASS();
    else FAIL("expected HW_ERR_PARAM");
    bus_i2c_close(bus);
}

static void test_err_str_known(void)
{
    TEST("hw_err_str(HW_OK) returns non-NULL string");
    const char* s = hw_err_str(HW_OK);
    if (s && strlen(s) > 0) PASS();
    else FAIL("expected non-empty string");
}

static void test_err_str_unknown(void)
{
    TEST("hw_err_str(999) returns 'Unknown error'");
    const char* s = hw_err_str((hw_err_t)999);
    if (s && strcmp(s, "Unknown error") == 0) PASS();
    else FAIL("expected 'Unknown error'");
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== hw I2C Bus Tests ===\n\n");

    test_open_null_device();
    test_open_invalid_device();
    test_close_null();
    test_transfer_null_bus();
    test_write_null_bus();
    test_read_null_bus();
    test_transfer_null_tx_buf();
    test_transfer_null_rx_buf();
    test_err_str_known();
    test_err_str_unknown();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
