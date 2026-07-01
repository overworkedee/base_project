# HW Test Demo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 将 `hw/demo/demo_main.c` 替换为命令行交互式硬测程序，支持 LED 开关/trigger 和 I2C 扫描。

**Architecture:** 单文件实现，`main` 循环读取 stdin，空格拆 token 后 `strcmp` 路由到 handler 函数。LED handler 每次 open/close。I2C handler 复用现有 `bus_i2c_open/read/close`。

**Tech Stack:** C11, hw lib (dev_led, bus_i2c, hw_error), CMake HW_BUILD_DEMO option

## Global Constraints

- 已有 `HW_BUILD_DEMO` option 不变，文件路径 `hw/demo/demo_main.c` 不变
- LED 操作 hardcode `"blue_led"` label
- I2C 扫描范围 `/dev/i2c-0` ~ `/dev/i2c-6`，地址 0x03-0x77
- 无独立测试文件，板上手动验证
- CMakeLists.txt 无需修改（demo 目标已配置）

---

### Task 1: 重写 demo_main.c 为交互式命令行程序

**Files:**
- Modify: `hw/demo/demo_main.c`

**Interfaces:**
- Consumes: `led_open`, `led_close`, `led_on`, `led_off`, `led_set_trigger`, `LED_TRIGGER_HEARTBEAT`, `LED_TRIGGER_NONE` from `hw/dev/dev_led.h`
- Consumes: `bus_i2c_open`, `bus_i2c_close`, `bus_i2c_read`, `HW_OK` from `hw/bus/bus_i2c.h` and `hw/hw_error.h`
- Produces: `hw_demo` executable (target unchanged)

- [ ] **Step 1: 写入新的 demo_main.c**

```c
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
```

将以上代码写入 `hw/demo/demo_main.c`（覆盖已有文件）。

- [ ] **Step 2: 构建验证**

```bash
cd /home/chenchizhao/project/build && cmake -DHW_BUILD_DEMO=ON .. && make -j$(nproc)
```

Expected: 编译成功，无警告无错误。生成 `hw/hw_demo` 可执行文件。

- [ ] **Step 3: 跑全部测试确保现有功能无回归**

```bash
cd /home/chenchizhao/project/build && ctest --output-on-failure
```

Expected: `test_bus_i2c`、`test_dev_led`、`test_log` 3/3 通过。

- [ ] **Step 4: Commit**

```bash
cd /home/chenchizhao/project && git add hw/demo/demo_main.c && git commit -m "feat(demo): replace demo with interactive CLI test program"
```

---

### Task 2: 最终验证

- [ ] **Step 1: 完整构建**

```bash
cd /home/chenchizhao/project/build && rm -rf * && cmake -DHW_BUILD_DEMO=ON .. && make -j$(nproc) && ctest --output-on-failure
```

Expected: 3/3 测试通过。

- [ ] **Step 2: 交叉编译**

```bash
cd /home/chenchizhao/project && ./build.sh
```

Expected: ARM64 交叉编译成功。

- [ ] **Step 3: Commit plan**

```bash
cd /home/chenchizhao/project && git add docs/superpowers/plans/2026-07-02-hw-test-demo.md && git commit -m "docs(plan): add HW test demo implementation plan"
```
