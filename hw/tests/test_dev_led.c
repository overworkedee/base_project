/**
 * @file test_dev_led.c
 * @brief LED 控制模块单元测试
 *
 * 测试策略：
 *   - NULL 参数测试在所有环境下运行（不需要实际 LED 硬件）
 *   - 集成测试在检测到实际 LED（blue_led）时才运行，否则跳过
 *
 * 测试格式遵循 test_bus_i2c.c 的 TEST/PASS/FAIL 宏风格。
 */

#include "hw/dev/dev_led.h"
#include "hw/hw_error.h"

#include <stdio.h>
#include <string.h>

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

#define SKIP(msg) do { \
    printf("SKIP (%s)\n", msg); \
} while(0)

/* ── NULL 参数测试（无需硬件） ────────────────────────────────────── */

static void test_open_null_name(void)
{
    TEST("led_open(NULL) returns NULL");
    led_t* led = led_open(NULL);
    if (led == NULL) PASS();
    else { led_close(led); FAIL("expected NULL"); }
}

static void test_open_nonexistent(void)
{
    TEST("led_open(nonexistent) returns NULL");
    led_t* led = led_open("nonexistent_led_xyz_12345");
    if (led == NULL) PASS();
    else { led_close(led); FAIL("expected NULL"); }
}

static void test_close_null(void)
{
    TEST("led_close(NULL) does not crash");
    led_close(NULL);
    PASS();
}

static void test_on_null(void)
{
    TEST("led_on(NULL) returns HW_ERR_PARAM");
    hw_err_t ret = led_on(NULL);
    if (ret == HW_ERR_PARAM) PASS();
    else FAIL("expected HW_ERR_PARAM");
}

static void test_off_null(void)
{
    TEST("led_off(NULL) returns HW_ERR_PARAM");
    hw_err_t ret = led_off(NULL);
    if (ret == HW_ERR_PARAM) PASS();
    else FAIL("expected HW_ERR_PARAM");
}

static void test_set_trigger_null(void)
{
    TEST("led_set_trigger(NULL, ...) returns HW_ERR_PARAM");
    hw_err_t ret = led_set_trigger(NULL, LED_TRIGGER_HEARTBEAT);
    if (ret == HW_ERR_PARAM) PASS();
    else FAIL("expected HW_ERR_PARAM");
}

static void test_get_brightness_null_led(void)
{
    TEST("led_get_brightness(NULL, ...) returns HW_ERR_PARAM");
    int brightness = -1;
    hw_err_t ret = led_get_brightness(NULL, &brightness);
    if (ret == HW_ERR_PARAM) PASS();
    else FAIL("expected HW_ERR_PARAM");
}

static void test_get_trigger_null_led(void)
{
    TEST("led_get_trigger(NULL, ...) returns HW_ERR_PARAM");
    led_trigger_t trigger = (led_trigger_t)-1;
    hw_err_t ret = led_get_trigger(NULL, &trigger);
    if (ret == HW_ERR_PARAM) PASS();
    else FAIL("expected HW_ERR_PARAM");
}

/* ── 集成测试（需要实际 LED 硬件，不可用时跳过） ──────────────────── */

static void test_on_off_cycle(void)
{
    TEST("led_on/led_off cycle on blue_led");
    led_t* led = led_open("blue_led");
    if (!led) { SKIP("blue_led not available"); return; }

    hw_err_t ret;

    /* 先打开 */
    ret = led_on(led);
    if (ret != HW_OK) { led_close(led); FAIL("led_on failed"); return; }

    /* 读取亮度应为 > 0 */
    int brightness = 0;
    ret = led_get_brightness(led, &brightness);
    if (ret != HW_OK) { led_close(led); FAIL("led_get_brightness failed"); return; }
    if (brightness <= 0) { led_close(led); FAIL("brightness should be > 0 after led_on"); return; }

    /* 关闭 */
    ret = led_off(led);
    if (ret != HW_OK) { led_close(led); FAIL("led_off failed"); return; }

    /* 读取亮度应为 0 */
    ret = led_get_brightness(led, &brightness);
    if (ret != HW_OK) { led_close(led); FAIL("led_get_brightness after off failed"); return; }
    if (brightness != 0) { led_close(led); FAIL("brightness should be 0 after led_off"); return; }

    led_close(led);
    PASS();
}

static void test_set_trigger_cycle(void)
{
    TEST("led_set_trigger heartbeat/none cycle on blue_led");
    led_t* led = led_open("blue_led");
    if (!led) { SKIP("blue_led not available"); return; }

    hw_err_t ret;
    led_trigger_t trigger;

    /* 设置为 heartbeat */
    ret = led_set_trigger(led, LED_TRIGGER_HEARTBEAT);
    if (ret != HW_OK) { led_close(led); FAIL("set trigger heartbeat failed"); return; }

    /* 读回验证 */
    ret = led_get_trigger(led, &trigger);
    if (ret != HW_OK) { led_close(led); FAIL("get trigger after heartbeat failed"); return; }
    if (trigger != LED_TRIGGER_HEARTBEAT) {
        led_close(led);
        FAIL("expected LED_TRIGGER_HEARTBEAT after set");
        return;
    }

    /* 设置为 none */
    ret = led_set_trigger(led, LED_TRIGGER_NONE);
    if (ret != HW_OK) { led_close(led); FAIL("set trigger none failed"); return; }

    /* 读回验证 */
    ret = led_get_trigger(led, &trigger);
    if (ret != HW_OK) { led_close(led); FAIL("get trigger after none failed"); return; }
    if (trigger != LED_TRIGGER_NONE) {
        led_close(led);
        FAIL("expected LED_TRIGGER_NONE after set");
        return;
    }

    led_close(led);
    PASS();
}

static void test_get_brightness_null_buf(void)
{
    TEST("led_get_brightness(valid_led, NULL) returns HW_ERR_PARAM");
    led_t* led = led_open("blue_led");
    if (!led) { SKIP("blue_led not available"); return; }

    hw_err_t ret = led_get_brightness(led, NULL);
    if (ret == HW_ERR_PARAM) PASS();
    else FAIL("expected HW_ERR_PARAM");

    led_close(led);
}

static void test_get_trigger_null_buf(void)
{
    TEST("led_get_trigger(valid_led, NULL) returns HW_ERR_PARAM");
    led_t* led = led_open("blue_led");
    if (!led) { SKIP("blue_led not available"); return; }

    hw_err_t ret = led_get_trigger(led, NULL);
    if (ret == HW_ERR_PARAM) PASS();
    else FAIL("expected HW_ERR_PARAM");

    led_close(led);
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== hw LED Control Tests ===\n\n");

    /* NULL 参数测试（始终运行） */
    test_open_null_name();
    test_open_nonexistent();
    test_close_null();
    test_on_null();
    test_off_null();
    test_set_trigger_null();
    test_get_brightness_null_led();
    test_get_trigger_null_led();

    /* 集成测试（需要实际硬件） */
    test_on_off_cycle();
    test_set_trigger_cycle();
    test_get_brightness_null_buf();
    test_get_trigger_null_buf();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
