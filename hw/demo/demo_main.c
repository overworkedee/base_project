/**
 * demo_main.c — HW 硬测命令行交互程序
 *
 * 编译: cmake -DHW_BUILD_DEMO=ON ..
 * 运行: ./hw/hw_demo
 *
 * 支持命令:
 *   help           — 显示帮助
 *   led on         — 打开 blue_led
 *   led off        — 关闭 blue_led
 *   led heartbeat  — blue_led 心跳闪烁
 *   led none       — blue_led 停止闪烁
 *   i2c scan       — 扫描 I2C 总线设备
 *   quit           — 退出
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hw/dev/dev_led.h"
#include "hw/bus/bus_i2c.h"
#include "hw/hw_error.h"

/* ── 常量 ─────────────────────────────────────────────────────────── */

#define MAX_INPUT    256
#define LED_NAME     "blue_led"
#define I2C_BUS_MIN  0
#define I2C_BUS_MAX  6

/* ── 帮助 ─────────────────────────────────────────────────────────── */

/**
 * 打印所有命令及格式。
 */
static void cmd_help(void)
{
    printf(
        "\n"
        "=== HW Test Commands ===\n"
        "\n"
        "  help              Show this help\n"
        "  led on            Turn on blue_led\n"
        "  led off           Turn off blue_led\n"
        "  led heartbeat     Set blue_led trigger to heartbeat\n"
        "  led none          Set blue_led trigger to none\n"
        "  i2c scan          Scan /dev/i2c-0 .. /dev/i2c-6 for devices\n"
        "  quit              Exit\n"
        "\n"
    );
}

/* ── LED handler ──────────────────────────────────────────────────── */

/**
 * 处理 led 子命令。
 *
 * 每次操作独立 open/close blue_led。
 */
static void cmd_led(const char* subcmd)
{
    if (!subcmd) {
        printf("[LED] usage: led <on|off|heartbeat|none>\n");
        return;
    }

    led_t* led = led_open(LED_NAME);
    if (!led) {
        printf("[LED] failed to open '%s' — check sysfs path\n", LED_NAME);
        return;
    }

    if (strcmp(subcmd, "on") == 0) {
        hw_err_t ret = led_on(led);
        printf("[LED] on  -> %s\n", (ret == HW_OK) ? "OK" : hw_err_str(ret));
    } else if (strcmp(subcmd, "off") == 0) {
        hw_err_t ret = led_off(led);
        printf("[LED] off -> %s\n", (ret == HW_OK) ? "OK" : hw_err_str(ret));
    } else if (strcmp(subcmd, "heartbeat") == 0) {
        hw_err_t ret = led_set_trigger(led, LED_TRIGGER_HEARTBEAT);
        printf("[LED] trigger=heartbeat -> %s\n",
               (ret == HW_OK) ? "OK" : hw_err_str(ret));
    } else if (strcmp(subcmd, "none") == 0) {
        hw_err_t ret = led_set_trigger(led, LED_TRIGGER_NONE);
        printf("[LED] trigger=none -> %s\n",
               (ret == HW_OK) ? "OK" : hw_err_str(ret));
    } else {
        printf("[LED] unknown subcommand '%s'\n", subcmd);
    }

    led_close(led);
}

/* ── I2C handler ──────────────────────────────────────────────────── */

/**
 * 扫描一条 I2C 总线上的设备。
 *
 * 对 7-bit 地址 0x03–0x77 逐一发 1 字节读，ACK 表示设备存在。
 */
static void scan_i2c_bus(int busnum)
{
    char device[32];
    snprintf(device, sizeof(device), "/dev/i2c-%d", busnum);

    bus_i2c_t* bus = bus_i2c_open(device);
    if (!bus) {
        printf("  %-16s — not available\n", device);
        return;
    }

    printf("  %-16s — opened, scanning...\n", device);

    int found = 0;
    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        uint8_t dummy;
        hw_err_t ret = bus_i2c_read(bus, addr, &dummy, 1);
        if (ret == HW_OK) {
            printf("    Device at 0x%02x\n", addr);
            found++;
        }
    }

    if (found == 0) {
        printf("    No devices detected\n");
    }

    bus_i2c_close(bus);
}

/**
 * 处理 i2c 子命令。
 */
static void cmd_i2c(const char* subcmd)
{
    if (!subcmd || strcmp(subcmd, "scan") != 0) {
        printf("[I2C] usage: i2c scan\n");
        return;
    }

    printf("\n--- I2C Bus Scan ---\n");
    for (int i = I2C_BUS_MIN; i <= I2C_BUS_MAX; i++) {
        scan_i2c_bus(i);
    }
    printf("--- Scan complete ---\n\n");
}

/* ── Main loop ────────────────────────────────────────────────────── */

int main(void)
{
    char input[MAX_INPUT];

    printf("=== HW Test Demo ===\n");
    printf("Type 'help' for available commands.\n\n");

    while (1) {
        printf("hw> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) {
            printf("\n");
            break;  /* EOF (Ctrl+D) */
        }

        /* 去除尾部换行 */
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') input[len - 1] = '\0';
        if (len == 0) continue;

        /* 拆 token */
        char* cmd  = strtok(input, " ");
        char* arg1 = strtok(NULL, " ");

        if (!cmd) continue;

        if (strcmp(cmd, "help") == 0) {
            cmd_help();
        } else if (strcmp(cmd, "led") == 0) {
            cmd_led(arg1);
        } else if (strcmp(cmd, "i2c") == 0) {
            cmd_i2c(arg1);
        } else if (strcmp(cmd, "quit") == 0) {
            printf("Bye.\n");
            break;
        } else {
            printf("Unknown command: '%s', type 'help' for usage\n", cmd);
        }
    }

    return 0;
}
