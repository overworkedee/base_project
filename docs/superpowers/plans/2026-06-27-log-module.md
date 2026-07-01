# Log 日志模块 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `modules/log/` as independent static library `liblog.a` — debug/info/warn/error macros with file+terminal output, thread-safe, compile-time debug gating via env file.

**Architecture:** Macro-based interface (`LOG_INFO`, `LOG_ERROR`, etc.) wrapping a single internal function `log_write_impl`. Macros capture `__FILE__`/`__LINE__`/`__func__` automatically. Internal mutex (`hw_mutex.h`) serializes output. Env variable `LOG_ENABLE_DEBUG` controls debug-level compilation.

**Tech Stack:** C11, pthread (via `hw/mutex.h`), `<stdio.h>` + `<time.h>` for file I/O and timestamps.

## Global Constraints

- 所有函数使用中文注释，含 `@param`、`@return`、`@note`
- 日志文件路径：`/tmp/project.log`
- 输出格式：`[YYYY-MM-DD HH:MM:SS.ms] [LEVEL] [file:line func] message`
- 线程安全：内部 `pthread_mutex_t`，使用 `hw/hw_mutex.h`
- DEBUG 裁剪：env 文件 `LOG_ENABLE_DEBUG`，顶层 CMakeLists.txt 统一转为 `-DLOG_ENABLE_DEBUG`
- 编译验证：先 `source env` 再 `${CROSS_COMPILE}gcc`
- 日志等级：debug(0) / info(1) / warn(2) / error(3)

## File Structure Map

```
modules/
├── CMakeLists.txt                  # Task 1 — add_subdirectory(log)
└── log/
    ├── CMakeLists.txt              # Task 1 — liblog.a + demo
    ├── include/
    │   └── log/
    │       └── log.h               # Task 2 — enum + macros + API
    ├── src/
    │   └── log.c                   # Task 3 — log_write_impl + init/deinit
    ├── tests/
    │   └── test_log.c              # Task 4 — unit tests
    └── demo/
        └── log_demo.c              # Task 5 — usage demo
```

Modify:
- `env/rk3588_product_orangerpi5plus.env` — Task 6 — add `LOG_ENABLE_DEBUG`
- `CMakeLists.txt` — Task 6 — add `add_subdirectory(modules)` + `add_definitions`

---

### Task 1: Scaffold directories and CMakeLists.txt

**Files:**
- Create: `modules/CMakeLists.txt`
- Create: `modules/log/CMakeLists.txt`

**Interfaces:**
- Produces: `liblog` CMake target, `modules/` build integration

- [ ] **Step 1: Create all directories**

```bash
mkdir -p modules/log/include/log modules/log/src modules/log/tests modules/log/demo
```

- [ ] **Step 2: Write modules/CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)

add_subdirectory(log)
```

- [ ] **Step 3: Write modules/log/CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)

set(LOG_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/log.c
)

set(LOG_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/log/log.h
)

# 静态库 liblog.a
add_library(log STATIC ${LOG_SOURCES} ${LOG_HEADERS})
target_include_directories(log PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(log PRIVATE hw pthread)

# 单元测试（host 编译，非交叉编译）
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(test_log ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_log.c)
    target_link_libraries(test_log log)
    add_test(NAME test_log COMMAND test_log)
endif()

# Demo
add_executable(log_demo ${CMAKE_CURRENT_SOURCE_DIR}/demo/log_demo.c)
target_link_libraries(log_demo log)
```

- [ ] **Step 4: Verify CMake parses**

```bash
cd /home/chenchizhao/project/build
rm -rf CMakeCache.txt CMakeFiles/
cmake .. 2>&1 | tail -10
```

Expected: no errors related to modules/ (will fail until top-level `add_subdirectory(modules)` is added in Task 6).

- [ ] **Step 5: Commit**

```bash
git add modules/
git commit -m "feat(log): scaffold modules/ and log/ directory with CMakeLists.txt"
```

---

### Task 2: log.h — header with enum, macros, and API

**Files:**
- Create: `modules/log/include/log/log.h`

**Interfaces:**
- Consumes: `hw_err_t` from `hw/hw_error.h`
- Produces: `log_level_t` enum, `LOG_DEBUG/INFO/WARN/ERROR` macros, `log_init/set_level/deinit` declarations

- [ ] **Step 1: Write log.h**

```c
#ifndef LOG_H
#define LOG_H

#include <stddef.h>
#include <stdint.h>
#include "hw/hw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 日志等级 ────────────────────────────────────────────────────── */

/**
 * 日志等级枚举，值越小越详细。
 */
typedef enum {
    LOG_DEBUG = 0,   /* 调试信息，仅开发期使用           */
    LOG_INFO  = 1,   /* 正常运行信息                     */
    LOG_WARN  = 2,   /* 警告，不影响运行但值得关注       */
    LOG_ERROR = 3,   /* 错误，功能受损但程序可继续       */
} log_level_t;

/* ── 生命周期 ────────────────────────────────────────────────────── */

/**
 * 初始化日志模块，打开日志文件并设置最低输出等级。
 *
 * 必须在任何 LOG_* 宏使用前调用。
 *
 * @param file_path  日志文件路径，如 "/tmp/project.log"
 * @param min_level  最低记录等级，低于此等级的日志调用将被丢弃
 * @return           HW_OK 成功，其他值失败
 */
hw_err_t log_init(const char* file_path, log_level_t min_level);

/**
 * 运行时动态修改最低日志等级。
 *
 * @param level  新的最低等级，LOG_DEBUG 输出最多，LOG_ERROR 输出最少
 */
void log_set_level(log_level_t level);

/**
 * 关闭日志模块，刷新缓冲区并关闭日志文件。
 *
 * 程序退出前调用。重复调用安全。
 */
void log_deinit(void);

/* ── 底层实现（通常不直接调用）──────────────────────────────────── */

/**
 * 日志写入核心函数，由宏自动捕获位置信息后调用。
 *
 * @param level  日志等级
 * @param file   源文件名（__FILE__）
 * @param line   行号（__LINE__）
 * @param func   函数名（__func__）
 * @param fmt    格式化字符串，遵循 printf 约定
 * @param ...    可变参数
 * @note         内部持有互斥锁，多线程安全
 */
void log_write_impl(log_level_t level, const char* file, int line,
                    const char* func, const char* fmt, ...)
    __attribute__((format(printf, 5, 6)));

/* ── 公开宏 ──────────────────────────────────────────────────────── */

#define LOG_WRITE(level, fmt, ...) \
    log_write_impl(level, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

/* DEBUG 等级受编译期宏控制，LOG_ENABLE_DEBUG=0 时编译为空操作 */
#if LOG_ENABLE_DEBUG
#define LOG_DEBUG(fmt, ...)  LOG_WRITE(LOG_DEBUG, fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...)  ((void)0)
#endif

#define LOG_INFO(fmt, ...)   LOG_WRITE(LOG_INFO,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)   LOG_WRITE(LOG_WARN,  fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)  LOG_WRITE(LOG_ERROR, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
```

- [ ] **Step 2: Verify header parses**

```bash
source /home/chenchizhao/project/env/rk3588_product_orangerpi5plus.env
echo '#include "log/log.h"
int main(void) {
    log_init("/tmp/test.log", LOG_DEBUG);
    LOG_INFO("hello %s", "world");
    LOG_DEBUG("debug msg");
    LOG_WARN("warn msg");
    LOG_ERROR("error msg");
    log_deinit();
    return 0;
}' | ${CROSS_COMPILE}gcc -fsyntax-only -xc \
    -I /home/chenchizhao/project/hw/include \
    -I /home/chenchizhao/project/modules/log/include \
    -DLOG_ENABLE_DEBUG=1 \
    -
```

Expected: parse success.

- [ ] **Step 3: Commit**

```bash
git add modules/log/include/log/log.h
git commit -m "feat(log): add log header with debug/info/warn/error macros"
```

---

### Task 3: log.c — implementation

**Files:**
- Create: `modules/log/src/log.c`

**Interfaces:**
- Consumes: `log.h`, `hw_mutex.h`, `hw_error.h`
- Produces: `log_init/log_set_level/log_deinit/log_write_impl` implementations

- [ ] **Step 1: Write log.c**

```c
/**
 * log.c — 日志模块核心实现
 *
 * 内部持有全局日志配置（文件指针、最低等级、互斥锁），
 * 所有 LOG_* 宏最终调用 log_write_impl() 完成格式化输出。
 */
#include "log/log.h"
#include "hw/hw_mutex.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

/* ── 全局状态 ────────────────────────────────────────────────────── */

/* 内部：日志模块全局配置 */
static struct {
    FILE*           fp;         /* 日志文件句柄                   */
    log_level_t     min_level;  /* 运行时最低输出等级             */
    hw_mutex_t      lock;       /* 互斥锁                         */
    _Bool           initialized;/* 是否已初始化                   */
} g_log = { NULL, LOG_INFO, PTHREAD_MUTEX_INITIALIZER, 0 };

/* ── 内部函数 ────────────────────────────────────────────────────── */

/* 内部：获取带毫秒的当前时间字符串 */
static void _make_timestamp(char* buf, size_t len)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    struct tm* tm_info = localtime(&tv.tv_sec);
    size_t pos = strftime(buf, len, "%Y-%m-%d %H:%M:%S", tm_info);
    snprintf(buf + pos, len - pos, ".%03ld", tv.tv_usec / 1000);
}

/* 内部：日志等级转可读字符串 */
static const char* _level_str(log_level_t level)
{
    switch (level) {
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO:  return "INFO ";
    case LOG_WARN:  return "WARN ";
    case LOG_ERROR: return "ERROR";
    default:        return "?????";
    }
}

/* ── 生命周期 ────────────────────────────────────────────────────── */

hw_err_t log_init(const char* file_path, log_level_t min_level)
{
    if (!file_path) return HW_ERR_PARAM;

    /* 如果已初始化，先关闭旧文件 */
    if (g_log.initialized) {
        log_deinit();
    }

    hw_err_t ret = hw_mutex_init(&g_log.lock);
    if (ret != HW_OK) return ret;

    g_log.fp = fopen(file_path, "a");
    if (!g_log.fp) {
        hw_mutex_destroy(&g_log.lock);
        return HW_ERR_BUS_OPEN;
    }

    /* 无缓冲，每次写入立即落盘 */
    setvbuf(g_log.fp, NULL, _IONBF, 0);

    g_log.min_level  = min_level;
    g_log.initialized = 1;

    LOG_INFO("===== log module initialized, level=%d =====", min_level);
    return HW_OK;
}

void log_set_level(log_level_t level)
{
    g_log.min_level = level;
}

void log_deinit(void)
{
    if (!g_log.initialized) return;

    LOG_INFO("===== log module shutdown =====");

    if (g_log.fp) {
        fflush(g_log.fp);
        fclose(g_log.fp);
        g_log.fp = NULL;
    }

    hw_mutex_destroy(&g_log.lock);
    g_log.initialized = 0;
}

/* ── 日志写入 ────────────────────────────────────────────────────── */

void log_write_impl(log_level_t level, const char* file, int line,
                    const char* func, const char* fmt, ...)
{
    /* 运行时等级过滤 */
    if (level < g_log.min_level) return;
    if (!g_log.initialized) return;

    /* 时间戳 */
    char time_buf[32];
    _make_timestamp(time_buf, sizeof(time_buf));

    /* 从完整路径中提取文件名（仅取最后一段） */
    const char* fname = strrchr(file, '/');
    fname = fname ? fname + 1 : file;

    hw_mutex_lock(&g_log.lock);

    /* 格式: [时间] [等级] [文件:行号 函数名] */
    char header[128];
    int hdr_len = snprintf(header, sizeof(header),
                           "[%s] [%s] [%s:%d %s] ",
                           time_buf, _level_str(level), fname, line, func);

    /* 消息体 */
    va_list args;
    va_start(args, fmt);
    char msg[512];
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    /* 输出到文件和终端 */
    fprintf(g_log.fp, "%s%s\n", header, msg);
    fprintf(stdout, "%s%s\n", header, msg);

    hw_mutex_unlock(&g_log.lock);
}
```

- [ ] **Step 2: Compile object file**

```bash
source /home/chenchizhao/project/env/rk3588_product_orangerpi5plus.env
cd /home/chenchizhao/project
${CROSS_COMPILE}gcc -c modules/log/src/log.c \
    -o /tmp/log.o \
    -I hw/include \
    -I modules/log/include \
    -DLOG_ENABLE_DEBUG=1 \
    -Wall -Wextra -std=c11
rm -f /tmp/log.o
```

Expected: compile success, no warnings.

- [ ] **Step 3: Commit**

```bash
git add modules/log/src/log.c
git commit -m "feat(log): implement log_write_impl with mutex and timestamp"
```

---

### Task 4: test_log.c — unit tests

**Files:**
- Create: `modules/log/tests/test_log.c`

**Interfaces:**
- Consumes: `log.h`
- Produces: 10 test cases covering init, write, level filter, deinit, edge cases

- [ ] **Step 1: Write test_log.c**

```c
/**
 * test_log.c — 日志模块单元测试
 *
 * 使用 host gcc 编译运行，不需要 I2C 硬件。
 * 测试 init/deinit、等级过滤、格式输出等核心路径。
 */
#include "log/log.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int passed = 0;
static int failed = 0;

#define TEST(name)  printf("  TEST: %s ... ", name)
#define PASS()      do { printf("PASS\n"); passed++; } while(0)
#define FAIL(msg)   do { printf("FAIL: %s\n", msg); failed++; } while(0)

#define LOG_FILE "/tmp/test_log_module.log"

static void rm_log(void) { unlink(LOG_FILE); }

/* ── 测试用例 ─────────────────────────────────────────────────────── */

/* 测试：正常初始化 */
static void test_init_ok(void)
{
    TEST("log_init returns HW_OK");
    rm_log();
    hw_err_t ret = log_init(LOG_FILE, LOG_DEBUG);
    if (ret == HW_OK) PASS();
    else FAIL("init failed");
}

/* 测试：NULL 路径初始化 */
static void test_init_null_path(void)
{
    TEST("log_init(NULL) returns HW_ERR_PARAM");
    hw_err_t ret = log_init(NULL, LOG_INFO);
    if (ret == HW_ERR_PARAM) PASS();
    else FAIL("expected HW_ERR_PARAM");
}

/* 测试：重复初始化（自动 deinit 旧实例） */
static void test_reinit(void)
{
    TEST("log_init twice does not crash");
    rm_log();
    log_init(LOG_FILE, LOG_DEBUG);
    /* 第二次 init 应自动关闭第一次 */
    hw_err_t ret = log_init(LOG_FILE, LOG_INFO);
    if (ret == HW_OK) PASS();
    else FAIL("re-init failed");
}

/* 测试：日志文件存在 */
static void test_file_created(void)
{
    TEST("log file exists after init");
    rm_log();
    log_init(LOG_FILE, LOG_DEBUG);
    FILE* fp = fopen(LOG_FILE, "r");
    if (fp) { fclose(fp); PASS(); }
    else FAIL("file not found");
}

/* 测试：等级过滤 — DEBUG 低于 INFO 应被过滤 */
static void test_level_filter(void)
{
    TEST("LOG_DEBUG filtered when min_level=LOG_INFO");
    rm_log();
    log_init(LOG_FILE, LOG_INFO);
    LOG_DEBUG("should be filtered");
    LOG_INFO("should appear");
    log_deinit();
    /* 验证文件中没有 DEBUG 消息 */
    FILE* fp = fopen(LOG_FILE, "r");
    if (!fp) { FAIL("file not found"); return; }
    char line[256];
    int has_debug = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "DEBUG") && strstr(line, "should be filtered"))
            has_debug = 1;
    }
    fclose(fp);
    if (!has_debug) PASS();
    else FAIL("DEBUG message appeared despite LOG_INFO min_level");
}

/* 测试：LOG_ERROR 始终输出 */
static void test_error_always(void)
{
    TEST("LOG_ERROR appears even with min_level=LOG_ERROR");
    rm_log();
    log_init(LOG_FILE, LOG_ERROR);
    LOG_DEBUG("nope");
    LOG_INFO("nope too");
    LOG_WARN("nope three");
    LOG_ERROR("must appear");
    log_deinit();
    FILE* fp = fopen(LOG_FILE, "r");
    if (!fp) { FAIL("file not found"); return; }
    char content[2048] = {0};
    size_t n = fread(content, 1, sizeof(content) - 1, fp);
    fclose(fp);
    content[n] = '\0';
    if (strstr(content, "must appear")) PASS();
    else FAIL("LOG_ERROR missing");
}

/* 测试：文件内容包含时间戳 */
static void test_has_timestamp(void)
{
    TEST("log line contains timestamp");
    rm_log();
    log_init(LOG_FILE, LOG_DEBUG);
    LOG_INFO("timestamp test");
    log_deinit();
    FILE* fp = fopen(LOG_FILE, "r");
    if (!fp) { FAIL("file not found"); return; }
    char line[256];
    int has_ts = 0;
    while (fgets(line, sizeof(line), fp)) {
        /* 时间格式: YYYY-MM-DD HH:MM:SS.ms */
        if (strstr(line, "202") && strstr(line, ":")) has_ts = 1;
    }
    fclose(fp);
    if (has_ts) PASS();
    else FAIL("no timestamp found");
}

/* 测试：文件内容包含函数名 */
static void test_has_funcname(void)
{
    TEST("log line contains function name");
    rm_log();
    log_init(LOG_FILE, LOG_DEBUG);
    LOG_INFO("funcname test");
    log_deinit();
    FILE* fp = fopen(LOG_FILE, "r");
    if (!fp) { FAIL("file not found"); return; }
    char content[2048] = {0};
    fread(content, 1, sizeof(content) - 1, fp);
    fclose(fp);
    if (strstr(content, "test_has_funcname")) PASS();
    else FAIL("function name not in output");
}

/* 测试：未初始化时调用宏不崩溃 */
static void test_not_initialized_safe(void)
{
    TEST("LOG_INFO without init() does not crash");
    /* 确保未初始化 */
    log_deinit();
    /* 应安全返回而不会 SEGV */
    LOG_INFO("this should be safe");
    PASS();
}

/* 测试：格式化输出 */
static void test_format(void)
{
    TEST("printf-style formatting works");
    rm_log();
    log_init(LOG_FILE, LOG_DEBUG);
    LOG_INFO("value=0x%02x, name=%s", 0x42, "test");
    log_deinit();
    FILE* fp = fopen(LOG_FILE, "r");
    if (!fp) { FAIL("file not found"); return; }
    char content[2048] = {0};
    fread(content, 1, sizeof(content) - 1, fp);
    fclose(fp);
    if (strstr(content, "value=0x42") && strstr(content, "name=test"))
        PASS();
    else FAIL("format not applied correctly");
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== Log Module Tests ===\n\n");

    test_init_ok();
    test_init_null_path();
    test_reinit();
    test_file_created();
    test_level_filter();
    test_error_always();
    test_has_timestamp();
    test_has_funcname();
    test_not_initialized_safe();
    test_format();

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);

    /* 清理 */
    rm_log();
    return failed > 0 ? 1 : 0;
}
```

- [ ] **Step 2: Build and run tests (host gcc)**

```bash
cd /home/chenchizhao/project
gcc -std=c11 -Wall -Wextra \
    modules/log/tests/test_log.c \
    modules/log/src/log.c \
    hw/src/hw_mutex.c \
    hw/src/hw_error.c \
    -I modules/log/include \
    -I hw/include \
    -DLOG_ENABLE_DEBUG=1 \
    -lpthread \
    -o /tmp/test_log
/tmp/test_log
```

Expected: all 10 tests PASS.

- [ ] **Step 3: Commit**

```bash
git add modules/log/tests/test_log.c
git commit -m "test(log): add 10 unit tests for log module"
```

---

### Task 5: log_demo.c — usage demo

**Files:**
- Create: `modules/log/demo/log_demo.c`

**Interfaces:**
- Consumes: `log.h`

- [ ] **Step 1: Write log_demo.c**

```c
/**
 * log_demo.c — 日志模块使用示例
 *
 * 演示四种日志等级的使用方式和输出格式。
 */
#include "log/log.h"

#include <stdio.h>
#include <unistd.h>

int main(void)
{
    printf("=== Log Module Demo ===\n\n");

    /* 初始化：DEBUG 等级，输出全部日志 */
    hw_err_t ret = log_init("/tmp/project.log", LOG_DEBUG);
    if (ret != HW_OK) {
        printf("log_init failed: %d\n", ret);
        return 1;
    }

    printf("Log module initialized.\n");
    printf("Log file: /tmp/project.log\n");
    printf("Min level: LOG_DEBUG\n\n");

    /* 四级日志演示 */
    LOG_DEBUG("这是一条 DEBUG 日志，变量 x=%d", 42);
    LOG_INFO("这是一条 INFO 日志，I2C 总线初始化成功");
    LOG_WARN("这是一条 WARN 日志，重试次数=%d", 3);
    LOG_ERROR("这是一条 ERROR 日志，设备无响应 addr=0x%02x", 0x77);

    /* 运行时切换等级 */
    printf("\n--- Switching to LOG_WARN ---\n\n");
    log_set_level(LOG_WARN);
    LOG_DEBUG("这条 DEBUG 不会被看到");
    LOG_INFO("这条 INFO 也不会被看到");
    LOG_WARN("这条 WARN 会被看到");
    LOG_ERROR("这条 ERROR 会被看到");

    /* 切回 DEBUG */
    printf("\n--- Switching back to LOG_DEBUG ---\n\n");
    log_set_level(LOG_DEBUG);
    LOG_DEBUG("DEBUG 日志恢复输出");

    /* 模拟一次带有多信息的日志 */
    LOG_INFO("传感器读取: addr=0x%02x, reg=0x%02x, value=%d", 0x42, 0x10, 128);

    printf("\n=== Demo Complete ===\n");
    printf("Check /tmp/project.log for output\n");

    log_deinit();
    return 0;
}
```

- [ ] **Step 2: Cross-compile demo**

```bash
source /home/chenchizhao/project/env/rk3588_product_orangerpi5plus.env
cd /home/chenchizhao/project
${CROSS_COMPILE}gcc -std=c11 -Wall -Wextra \
    modules/log/demo/log_demo.c \
    modules/log/src/log.c \
    hw/src/hw_mutex.c \
    hw/src/hw_error.c \
    -I modules/log/include \
    -I hw/include \
    -DLOG_ENABLE_DEBUG=1 \
    -lpthread \
    -o /tmp/log_demo
rm -f /tmp/log_demo
```

Expected: compile success, no warnings.

- [ ] **Step 3: Commit**

```bash
git add modules/log/demo/log_demo.c
git commit -m "feat(log): add log module usage demo"
```

---

### Task 6: Integration — env file + top-level CMakeLists

**Files:**
- Modify: `env/rk3588_product_orangerpi5plus.env`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add LOG_ENABLE_DEBUG to env file**

在 `env/rk3588_product_orangerpi5plus.env` 末尾追加：

```bash
# -------- 日志 --------
export LOG_ENABLE_DEBUG=1   # 1=启用DEBUG日志, 0=关闭（编译时裁剪）
```

- [ ] **Step 2: Update top-level CMakeLists.txt**

在现有 `add_subdirectory(${PROJECT_SOURCE_DIR}/hw)` 之后添加：

```cmake
# 日志 DEBUG 等级编译期裁剪（由 env 文件控制）
if(DEFINED ENV{LOG_ENABLE_DEBUG} AND $ENV{LOG_ENABLE_DEBUG} EQUAL 0)
    add_definitions(-DLOG_ENABLE_DEBUG=0)
else()
    add_definitions(-DLOG_ENABLE_DEBUG=1)
endif()

# Modules 目录
add_subdirectory(${PROJECT_SOURCE_DIR}/modules)
```

- [ ] **Step 3: Full build**

```bash
cd /home/chenchizhao/project && ./build.sh
```

Expected: `build/hello_world` 和 `build/modules/log/liblog.a` 编译成功。

- [ ] **Step 4: Verify liblog.a**

```bash
file /home/chenchizhao/project/build/modules/log/liblog.a
aarch64-none-linux-gnu-ar t /home/chenchizhao/project/build/modules/log/liblog.a
```

Expected: `liblog.a` 包含 `log.c.o`。

- [ ] **Step 5: Verify demo binary**

```bash
ls -lh /home/chenchizhao/project/build/modules/log/log_demo
file /home/chenchizhao/project/build/modules/log/log_demo
```

Expected: ARM aarch64 ELF executable.

- [ ] **Step 6: Commit**

```bash
git add env/rk3588_product_orangerpi5plus.env CMakeLists.txt
git commit -m "feat(build): integrate log module with env-controlled debug gating"
```

---

### Task 7: (Optional) Deploy and run on target

```bash
scp build/modules/log/log_demo root@<板子IP>:~/
ssh root@<板子IP> ./log_demo
ssh root@<板子IP> cat /tmp/project.log
```

Expected: 日志文件内容格式正确，等级过滤正常。
