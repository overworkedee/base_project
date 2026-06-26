/**
 * log.c -- 日志模块核心实现
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

/* -- 全局状态 ------------------------------------------------------ */

/* 内部：日志模块全局配置 */
static struct {
    FILE*           fp;         /* 日志文件句柄                   */
    log_level_t     min_level;  /* 运行时最低输出等级             */
    hw_mutex_t      lock;       /* 互斥锁                         */
    _Bool           initialized;/* 是否已初始化                   */
} g_log = { NULL, LOG_INFO, PTHREAD_MUTEX_INITIALIZER, 0 };

/* -- 内部函数 ------------------------------------------------------ */

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

/* -- 生命周期 ------------------------------------------------------ */

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

/* -- 日志写入 ------------------------------------------------------ */

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
    snprintf(header, sizeof(header),
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
