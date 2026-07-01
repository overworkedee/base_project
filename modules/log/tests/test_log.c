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
