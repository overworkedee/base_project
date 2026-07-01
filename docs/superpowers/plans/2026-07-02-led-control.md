# LED Control Module Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 Orange Pi 5 Plus 板载 LED 实现 sysfs 控制模块，支持开关、heartbeat/none trigger 切换、状态读取，线程安全。

**Architecture:** 新增 `dev_led.h`（头文件）和 `dev_led.c`（实现），遵循现有 hw/ 框架模式：不透明句柄 + 内部互斥锁 + sysfs 文件读写。错误码新增 `HW_ERR_IO`。测试遵循 `test_bus_i2c.c` 的 TEST/PASS/FAIL 宏风格。

**Tech Stack:** C11, sysfs (`/sys/class/leds/<name>/`), pthread mutex (via hw_mutex), CMake

## Global Constraints

- 所有公开函数使用中文注释，遵循 CLAUDE.md 的 `@param`/`@return`/`@note` 格式
- 线程安全：所有 IO 操作通过 `hw_mutex_t` 保护
- LED 名称由调用方运行时传入，不硬编码
- 错误码复用 `hw_err_t`，新增 `HW_ERR_IO` 表示通用文件 IO 失败

## File Structure

| 文件 | 操作 | 职责 |
|------|------|------|
| `hw/include/hw/hw_error.h` | 修改 | 新增 `HW_ERR_IO` 错误码 |
| `hw/src/hw_error.c` | 修改 | 新增 `HW_ERR_IO` 对应的字符串 |
| `hw/include/hw/dev/dev_led.h` | 创建 | 公开 API：类型定义 + 函数声明 |
| `hw/src/dev/dev_led.c` | 创建 | 实现：sysfs 读写 + mutex 保护 |
| `hw/CMakeLists.txt` | 修改 | 加入 `dev_led.c` 和测试目标 |
| `hw/tests/test_dev_led.c` | 创建 | 单元测试：参数校验 + 功能验证 |

---

### Task 1: 新增 HW_ERR_IO 错误码

**Files:**
- Modify: `hw/include/hw/hw_error.h:8-16`
- Modify: `hw/src/hw_error.c:6-14`

**Interfaces:**
- Produces: `HW_ERR_IO` 枚举值，供 Task 3 所有 sysfs 操作使用

- [ ] **Step 1: 在 hw_error.h 枚举中添加 HW_ERR_IO**

```c
typedef enum {
    HW_OK                = 0,
    HW_ERR_BUS_OPEN,          /* 总线打开失败 */
    HW_ERR_BUS_TRANSFER,      /* 传输失败 */
    HW_ERR_DEV_ADDR,          /* 设备地址无效 */
    HW_ERR_DEV_NOT_FOUND,     /* 设备无响应 */
    HW_ERR_MUTEX,             /* 互斥锁操作失败 */
    HW_ERR_PARAM,             /* 参数非法 */
    HW_ERR_IO,                /* 通用文件/IO 操作失败 */
} hw_err_t;
```

将 `hw/include/hw/hw_error.h` 第 8-16 行的枚举替换为以上代码，在第 15 行 `HW_ERR_PARAM` 之后新增 `HW_ERR_IO`。

- [ ] **Step 2: 在 hw_error.c 中添加对应字符串**

```c
case HW_ERR_IO:            return "IO operation failed";
```

将 `hw/src/hw_error.c` 第 12 行（`case HW_ERR_PARAM:`）之后插入以上行。

- [ ] **Step 3: 构建验证编译通过**

```bash
cd build && cmake .. && make -j$(nproc)
```

Expected: 编译成功，无错误。

- [ ] **Step 4: 运行现有测试确保无回归**

```bash
cd build && ctest --output-on-failure
```

Expected: `test_bus_i2c` 仍然全部通过。

- [ ] **Step 5: Commit**

```bash
git add hw/include/hw/hw_error.h hw/src/hw_error.c
git commit -m "feat(led): add HW_ERR_IO error code for sysfs IO failures"
```

---

### Task 2: 创建 LED 公开头文件

**Files:**
- Create: `hw/include/hw/dev/dev_led.h`

**Interfaces:**
- Produces: `led_trigger_t` 枚举, `led_t` 不透明类型, 所有公开函数声明

- [ ] **Step 1: 创建 dev_led.h**

```c
#ifndef DEV_LED_H
#define DEV_LED_H

#include <stddef.h>
#include "hw/hw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 类型定义 ─────────────────────────────────────────────────────── */

/** LED trigger 模式 */
typedef enum {
    LED_TRIGGER_NONE      = 0,  /* 无闪烁，常亮/常灭由 brightness 控制 */
    LED_TRIGGER_HEARTBEAT = 1,  /* 心跳闪烁，内核 heartbeat trigger       */
} led_trigger_t;

/** 不透明 LED 句柄 */
typedef struct led_ctx led_t;

/* ── 生命周期 ─────────────────────────────────────────────────────── */

/**
 * 打开 LED 设备。
 *
 * 校验 /sys/class/leds/<name>/ 路径是否存在，读取 max_brightness 并初始化互斥锁。
 *
 * @param name  LED 的 label 名称，对应设备树中的 label 属性，如 "blue_led"
 * @return      成功返回 LED 句柄，失败（路径不存在、内存不足）返回 NULL
 */
led_t* led_open(const char* name);

/**
 * 关闭 LED 设备并释放所有资源。
 *
 * @param led  LED 句柄，可为 NULL（此时无操作）
 */
void led_close(led_t* led);

/* ── 开关控制 ─────────────────────────────────────────────────────── */

/**
 * 打开 LED（写入 max_brightness 到 brightness 文件）。
 *
 * @param led  LED 句柄
 * @return     HW_OK 成功，HW_ERR_PARAM（led 为 NULL），HW_ERR_IO（写入失败）
 * @note       线程安全
 */
hw_err_t led_on(led_t* led);

/**
 * 关闭 LED（写入 0 到 brightness 文件）。
 *
 * @param led  LED 句柄
 * @return     HW_OK 成功，HW_ERR_PARAM（led 为 NULL），HW_ERR_IO（写入失败）
 * @note       线程安全
 */
hw_err_t led_off(led_t* led);

/* ── Trigger 控制 ─────────────────────────────────────────────────── */

/**
 * 设置 LED 闪烁模式。
 *
 * 向 /sys/class/leds/<name>/trigger 写入 trigger 名称。
 *
 * @param led      LED 句柄
 * @param trigger  目标 trigger 模式（LED_TRIGGER_NONE 或 LED_TRIGGER_HEARTBEAT）
 * @return         HW_OK 成功，HW_ERR_PARAM（参数非法），HW_ERR_IO（写入失败）
 * @note           线程安全
 */
hw_err_t led_set_trigger(led_t* led, led_trigger_t trigger);

/* ── 状态读取 ─────────────────────────────────────────────────────── */

/**
 * 读取 LED 当前亮度值。
 *
 * 从 /sys/class/leds/<name>/brightness 读取当前值。
 *
 * @param led         LED 句柄
 * @param brightness  输出参数，接收当前亮度值（0 ~ max_brightness）
 * @return            HW_OK 成功，HW_ERR_PARAM（参数为 NULL），HW_ERR_IO（读取失败）
 * @note              线程安全
 */
hw_err_t led_get_brightness(led_t* led, int* brightness);

/**
 * 读取 LED 当前 trigger 模式。
 *
 * 从 /sys/class/leds/<name>/trigger 读取，解析 [...] 中当前激活的 trigger。
 * 若当前 trigger 非 heartbeat/none，则返回 LED_TRIGGER_NONE。
 *
 * @param led      LED 句柄
 * @param trigger  输出参数，接收当前 trigger 模式
 * @return         HW_OK 成功，HW_ERR_PARAM（参数为 NULL），HW_ERR_IO（读取/解析失败）
 * @note           线程安全
 */
hw_err_t led_get_trigger(led_t* led, led_trigger_t* trigger);

#ifdef __cplusplus
}
#endif

#endif /* DEV_LED_H */
```

将以上内容写入 `hw/include/hw/dev/dev_led.h`。

- [ ] **Step 2: 构建验证头文件语法正确**

```bash
cd build && cmake .. && make -j$(nproc)
```

Expected: 编译成功。头文件被 CMake 的 `HW_HEADERS` 列表引用后 IDE 可索引，编译阶段暂不参与链接。

- [ ] **Step 3: Commit**

```bash
git add hw/include/hw/dev/dev_led.h
git commit -m "feat(led): add LED public header with API declarations"
```

---

### Task 3: 实现 LED 控制模块

**Files:**
- Create: `hw/src/dev/dev_led.c`

**Interfaces:**
- Consumes: `led_t`, `led_trigger_t`, `HW_ERR_IO`, `HW_ERR_PARAM`, `HW_OK` (from Task 1, Task 2)
- Consumes: `hw_mutex_init`, `hw_mutex_lock`, `hw_mutex_unlock`, `hw_mutex_destroy` (from `hw/hw_mutex.h`)
- Produces: `led_open`, `led_close`, `led_on`, `led_off`, `led_set_trigger`, `led_get_brightness`, `led_get_trigger`

- [ ] **Step 1: 创建 dev_led.c 实现文件**

```c
/**
 * @file dev_led.c
 * @brief LED sysfs 控制实现
 *
 * 通过 /sys/class/leds/<name>/ 接口控制 GPIO LED。
 * 内部使用 hw_mutex_t 保证所有 sysfs 操作的线程安全。
 */

#include "hw/dev/dev_led.h"
#include "hw/hw_mutex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* ── 内部结构 ─────────────────────────────────────────────────────── */

struct led_ctx {
    char        name[64];         /* LED 名称（label）                     */
    int         max_brightness;   /* 最大亮度值，从 max_brightness 读取     */
    hw_mutex_t  lock;             /* 线程安全锁                            */
};

/* ── 内部辅助函数 ─────────────────────────────────────────────────── */

/**
 * 向 sysfs 文件写入字符串。
 *
 * @param path   sysfs 文件路径
 * @param value  要写入的字符串
 * @return       HW_OK 成功，HW_ERR_IO 失败
 */
static hw_err_t led_write_sysfs(const char* path, const char* value)
{
    if (!path || !value) return HW_ERR_PARAM;

    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "[hw:led] fopen(%s) write failed: %s\n", path, strerror(errno));
        return HW_ERR_IO;
    }
    int ret = fputs(value, f);
    if (ret < 0) {
        fprintf(stderr, "[hw:led] fputs(%s) failed: %s\n", path, strerror(errno));
        fclose(f);
        return HW_ERR_IO;
    }
    fclose(f);
    return HW_OK;
}

/**
 * 从 sysfs 文件读取一行字符串（自动去除尾部换行符）。
 *
 * @param path  sysfs 文件路径
 * @param buf   输出缓冲区
 * @param len   缓冲区大小
 * @return      HW_OK 成功，HW_ERR_IO 失败，HW_ERR_PARAM 参数非法
 */
static hw_err_t led_read_sysfs(const char* path, char* buf, size_t len)
{
    if (!path || !buf || len == 0) return HW_ERR_PARAM;

    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "[hw:led] fopen(%s) read failed: %s\n", path, strerror(errno));
        return HW_ERR_IO;
    }
    if (!fgets(buf, (int)len, f)) {
        fprintf(stderr, "[hw:led] fgets(%s) failed: %s\n", path, strerror(errno));
        fclose(f);
        return HW_ERR_IO;
    }
    fclose(f);

    /* 去除尾部换行符 */
    size_t slen = strlen(buf);
    if (slen > 0 && buf[slen - 1] == '\n') {
        buf[slen - 1] = '\0';
    }
    return HW_OK;
}

/* ── 公开 API ─────────────────────────────────────────────────────── */

/**
 * 打开 LED 设备。
 *
 * 检查 /sys/class/leds/<name>/brightness 是否可写以验证 LED 存在，
 * 读取 max_brightness 作为后续 led_on 的目标值。
 *
 * @param name  LED 的 label 名称
 * @return      成功返回 LED 句柄，失败返回 NULL
 */
led_t* led_open(const char* name)
{
    if (!name) return NULL;

    /* 校验 sysfs 路径存在且可写 */
    char path[256];
    snprintf(path, sizeof(path), "/sys/class/leds/%s/brightness", name);
    if (access(path, W_OK) != 0) {
        fprintf(stderr, "[hw:led] LED '%s' not accessible at %s: %s\n",
                name, path, strerror(errno));
        return NULL;
    }

    /* 分配句柄 */
    led_t* led = (led_t*)calloc(1, sizeof(led_t));
    if (!led) {
        fprintf(stderr, "[hw:led] calloc failed for LED '%s'\n", name);
        return NULL;
    }
    strncpy(led->name, name, sizeof(led->name) - 1);
    led->name[sizeof(led->name) - 1] = '\0';

    /* 初始化互斥锁 */
    hw_err_t ret = hw_mutex_init(&led->lock);
    if (ret != HW_OK) {
        fprintf(stderr, "[hw:led] mutex init failed: %s\n", hw_err_str(ret));
        free(led);
        return NULL;
    }

    /* 读取 max_brightness */
    snprintf(path, sizeof(path), "/sys/class/leds/%s/max_brightness", name);
    char buf[32] = {0};
    ret = led_read_sysfs(path, buf, sizeof(buf));
    if (ret != HW_OK) {
        hw_mutex_destroy(&led->lock);
        free(led);
        return NULL;
    }
    led->max_brightness = atoi(buf);
    if (led->max_brightness <= 0) {
        led->max_brightness = 1;  /* 回退：至少为 1 */
    }

    return led;
}

/**
 * 关闭 LED 设备并释放所有资源。
 *
 * @param led  LED 句柄，可为 NULL
 */
void led_close(led_t* led)
{
    if (!led) return;
    hw_mutex_destroy(&led->lock);
    free(led);
}

/**
 * 打开 LED（写入 max_brightness）。
 *
 * @param led  LED 句柄
 * @return     HW_OK 成功，HW_ERR_PARAM 或 HW_ERR_IO 失败
 * @note       线程安全
 */
hw_err_t led_on(led_t* led)
{
    if (!led) return HW_ERR_PARAM;

    hw_err_t ret = hw_mutex_lock(&led->lock);
    if (ret != HW_OK) return ret;

    char path[256];
    char val[32];
    snprintf(path, sizeof(path), "/sys/class/leds/%s/brightness", led->name);
    snprintf(val, sizeof(val), "%d", led->max_brightness);
    ret = led_write_sysfs(path, val);

    hw_mutex_unlock(&led->lock);
    return ret;
}

/**
 * 关闭 LED（写入 0）。
 *
 * @param led  LED 句柄
 * @return     HW_OK 成功，HW_ERR_PARAM 或 HW_ERR_IO 失败
 * @note       线程安全
 */
hw_err_t led_off(led_t* led)
{
    if (!led) return HW_ERR_PARAM;

    hw_err_t ret = hw_mutex_lock(&led->lock);
    if (ret != HW_OK) return ret;

    char path[256];
    snprintf(path, sizeof(path), "/sys/class/leds/%s/brightness", led->name);
    ret = led_write_sysfs(path, "0");

    hw_mutex_unlock(&led->lock);
    return ret;
}

/**
 * 设置 LED trigger 模式。
 *
 * @param led      LED 句柄
 * @param trigger  目标 trigger 模式
 * @return         HW_OK 成功，HW_ERR_PARAM 或 HW_ERR_IO 失败
 * @note           线程安全
 */
hw_err_t led_set_trigger(led_t* led, led_trigger_t trigger)
{
    if (!led) return HW_ERR_PARAM;

    const char* trigger_str = NULL;
    switch (trigger) {
    case LED_TRIGGER_HEARTBEAT: trigger_str = "heartbeat"; break;
    case LED_TRIGGER_NONE:      trigger_str = "none";      break;
    default:                    return HW_ERR_PARAM;
    }

    hw_err_t ret = hw_mutex_lock(&led->lock);
    if (ret != HW_OK) return ret;

    char path[256];
    snprintf(path, sizeof(path), "/sys/class/leds/%s/trigger", led->name);
    ret = led_write_sysfs(path, trigger_str);

    hw_mutex_unlock(&led->lock);
    return ret;
}

/**
 * 读取 LED 当前亮度值。
 *
 * @param led         LED 句柄
 * @param brightness  输出参数，接收当前亮度值
 * @return            HW_OK 成功，HW_ERR_PARAM 或 HW_ERR_IO 失败
 * @note              线程安全
 */
hw_err_t led_get_brightness(led_t* led, int* brightness)
{
    if (!led || !brightness) return HW_ERR_PARAM;

    hw_err_t ret = hw_mutex_lock(&led->lock);
    if (ret != HW_OK) return ret;

    char path[256];
    char buf[32] = {0};
    snprintf(path, sizeof(path), "/sys/class/leds/%s/brightness", led->name);
    ret = led_read_sysfs(path, buf, sizeof(buf));
    if (ret == HW_OK) {
        *brightness = atoi(buf);
    }

    hw_mutex_unlock(&led->lock);
    return ret;
}

/**
 * 读取 LED 当前 trigger 模式。
 *
 * 解析 /sys/class/leds/<name>/trigger 文件中 [...] 包裹的当前激活项。
 *
 * @param led      LED 句柄
 * @param trigger  输出参数，接收当前 trigger 模式
 * @return         HW_OK 成功，HW_ERR_PARAM 或 HW_ERR_IO 失败
 * @note           线程安全。若当前 trigger 非 heartbeat/none，返回 LED_TRIGGER_NONE。
 */
hw_err_t led_get_trigger(led_t* led, led_trigger_t* trigger)
{
    if (!led || !trigger) return HW_ERR_PARAM;

    hw_err_t ret = hw_mutex_lock(&led->lock);
    if (ret != HW_OK) return ret;

    char path[256];
    char buf[1024] = {0};
    snprintf(path, sizeof(path), "/sys/class/leds/%s/trigger", led->name);
    ret = led_read_sysfs(path, buf, sizeof(buf));
    if (ret != HW_OK) {
        hw_mutex_unlock(&led->lock);
        return ret;
    }

    /* 解析 [...] 中当前激活的 trigger */
    char* start = strchr(buf, '[');
    char* end   = strchr(buf, ']');
    if (!start || !end || end <= start) {
        fprintf(stderr, "[hw:led] failed to parse trigger from %s\n", path);
        hw_mutex_unlock(&led->lock);
        return HW_ERR_IO;
    }
    *end = '\0';
    const char* active = start + 1;

    if (strcmp(active, "heartbeat") == 0) {
        *trigger = LED_TRIGGER_HEARTBEAT;
    } else {
        *trigger = LED_TRIGGER_NONE;
    }

    hw_mutex_unlock(&led->lock);
    return HW_OK;
}
```

将以上内容写入 `hw/src/dev/dev_led.c`。

- [ ] **Step 2: Commit**

```bash
git add hw/src/dev/dev_led.c
git commit -m "feat(led): implement LED sysfs control module"
```

---

### Task 4: 更新 CMakeLists.txt

**Files:**
- Modify: `hw/CMakeLists.txt:7-11`（HW_SOURCES）
- Modify: `hw/CMakeLists.txt:33-36`（测试目标区域）

**Interfaces:**
- Consumes: `hw/src/dev/dev_led.c` 源文件, `tests/test_dev_led.c` 测试文件 (from Task 5)

- [ ] **Step 1: 将 dev_led.c 加入 HW_SOURCES**

在 `hw/CMakeLists.txt` 第 10 行（`src/bus/bus_i2c.c`）之后新增一行：

```cmake
set(HW_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hw_error.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hw_mutex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bus/bus_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dev/dev_led.c
)
```

- [ ] **Step 2: 将 dev_led.h 加入 HW_HEADERS**

在 `hw/CMakeLists.txt` 第 19 行（`dev_template.h`）之后新增一行：

```cmake
set(HW_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/hw_error.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/hw_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/hw_mutex.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/bus/bus_i2c.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/dev/dev_template.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/dev/dev_led.h
)
```

- [ ] **Step 3: 在测试区域新增 test_dev_led 目标**

在 `hw/CMakeLists.txt` 末尾的 `add_test(NAME test_bus_i2c ...)` 之后追加：

```cmake
add_executable(test_dev_led ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dev_led.c)
target_link_libraries(test_dev_led hw)
add_test(NAME test_dev_led COMMAND test_dev_led)
```

- [ ] **Step 4: 构建验证**

```bash
cd build && cmake .. && make -j$(nproc)
```

Expected: 编译成功，`libhw.a` 包含 `dev_led.o`。

- [ ] **Step 5: Commit**

```bash
git add hw/CMakeLists.txt
git commit -m "build(led): add dev_led.c and test_dev_led to CMake"
```

---

### Task 5: 编写单元测试

**Files:**
- Create: `hw/tests/test_dev_led.c`

**Interfaces:**
- Consumes: `led_open`, `led_close`, `led_on`, `led_off`, `led_set_trigger`, `led_get_brightness`, `led_get_trigger` (from Task 3)
- Consumes: `HW_ERR_PARAM`, `HW_ERR_IO`, `HW_OK` (from Task 1)

- [ ] **Step 1: 创建 test_dev_led.c**

```c
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
```

将以上内容写入 `hw/tests/test_dev_led.c`。

- [ ] **Step 2: 构建并运行测试**

```bash
cd build && cmake .. && make -j$(nproc) && ctest --output-on-failure
```

Expected: 构建成功。若在开发机上，LED 集成测试会 SKIP（无 blue_led 路径），NULL 参数测试全部 PASS。若在 Orange Pi 5 Plus 上运行，集成测试也应 PASS。

- [ ] **Step 3: Commit**

```bash
git add hw/tests/test_dev_led.c
git commit -m "test(led): add unit tests for LED control module"
```

---

### Task 6: 最终构建与验证

**Files:**
- 无新文件，验证全项目编译

- [ ] **Step 1: 清理并完整构建**

```bash
cd /home/chenchizhao/project/build && rm -rf * && cmake .. && make -j$(nproc)
```

Expected: 全项目编译成功，无警告，无错误。

- [ ] **Step 2: 运行全部测试**

```bash
cd /home/chenchizhao/project/build && ctest --output-on-failure
```

Expected: `test_bus_i2c` 和 `test_dev_led` 均通过（LED 集成测试可能 SKIP）。

- [ ] **Step 3: 交叉编译验证**

```bash
cd /home/chenchizhao/project && ./build.sh
```

Expected: 交叉编译成功，生成 ARM64 二进制。

- [ ] **Step 4: 检查测试数量**

```bash
cd /home/chenchizhao/project/build && ctest -N
```

Expected: 显示 2 个测试（`test_bus_i2c` + `test_dev_led`）。

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/plans/2026-07-02-led-control.md
git commit -m "docs(plan): add LED control implementation plan"
```
