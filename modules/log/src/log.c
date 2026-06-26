/**
 * log.c -- 日志模块核心实现
 *
 * 内部持有全局日志配置（文件指针、最低等级、互斥锁），
 * 所有 LOG_* 宏最终调用 log_write_impl() 完成格式化输出。
 */
#define _GNU_SOURCE
#include "log/log.h"
#include "hw/hw_mutex.h"

#include <stdio.h>
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

/**
 * 内部：获取带毫秒的当前时间字符串
 *
 * @param buf  输出缓冲区
 * @param len  缓冲区大小
 * @return     无返回值
 * @note 使用 localtime_r 保证线程安全
 */
static void _make_timestamp(char* buf, size_t len)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    struct tm tm_buf;
    struct tm* tm_info = localtime_r(&tv.tv_sec, &tm_buf);
    size_t pos = strftime(buf, len, "%Y-%m-%d %H:%M:%S", tm_info);
    if (pos > 0 && pos < len) {
        snprintf(buf + pos, len - pos, ".%03ld", tv.tv_usec / 1000);
    }
}

/**
 * 内部：日志等级转可读字符串
 *
 * @param level  日志等级枚举值
 * @return       对应的等级字符串（固定长度5字符）
 * @note 未知等级返回 "?????"
 */
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

/**
 * 初始化日志模块
 *
 * 打开日志文件并初始化互斥锁。
 *
 * @param file_path  日志文件路径
 * @param min_level  最低输出等级
 * @return HW_OK 成功，HW_ERR_PARAM 参数为空，HW_ERR_BUS_OPEN 文件打开失败
 * @note 重复调用会自动关闭旧文件并重新初始化
 */
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
    if (setvbuf(g_log.fp, NULL, _IONBF, 0) != 0) {
        fprintf(stderr, "log: setvbuf failed for log file\n");
    }

    g_log.min_level  = min_level;
    g_log.initialized = 1;

    LOG_INFO("===== log module initialized, level=%d =====", min_level);
    return HW_OK;
}

/**
 * 设置运行时日志等级
 *
 * @param level  新的最低输出等级
 * @return       无返回值
 * @note 线程安全，通过互斥锁保护
 */
void log_set_level(log_level_t level)
{
    hw_err_t ret = hw_mutex_lock(&g_log.lock);
    if (ret != HW_OK) {
        fprintf(stderr, "log: hw_mutex_lock failed in log_set_level\n");
    }
    g_log.min_level = level;
    ret = hw_mutex_unlock(&g_log.lock);
    if (ret != HW_OK) {
        fprintf(stderr, "log: hw_mutex_unlock failed in log_set_level\n");
    }
}

/**
 * 反初始化日志模块
 *
 * 关闭日志文件、销毁互斥锁。
 *
 * @return     无返回值
 * @note 先持有锁再执行清理，避免与其他线程的 log_write_impl 并发冲突
 */
void log_deinit(void)
{
    hw_err_t ret = hw_mutex_lock(&g_log.lock);
    if (ret != HW_OK) {
        fprintf(stderr, "log: hw_mutex_lock failed in log_deinit\n");
        return;
    }

    if (!g_log.initialized) {
        hw_mutex_unlock(&g_log.lock);
        return;
    }

    g_log.initialized = 0;

    if (g_log.fp) {
        fclose(g_log.fp);
        g_log.fp = NULL;
    }

    ret = hw_mutex_unlock(&g_log.lock);
    if (ret != HW_OK) {
        fprintf(stderr, "log: hw_mutex_unlock failed in log_deinit\n");
    }

    ret = hw_mutex_destroy(&g_log.lock);
    if (ret != HW_OK) {
        fprintf(stderr, "log: hw_mutex_destroy failed in log_deinit\n");
    }
}

/* -- 日志写入 ------------------------------------------------------ */

/**
 * 日志写入内部实现
 *
 * 格式化日志消息并输出到文件和终端。
 *
 * @param level  日志等级
 * @param file   源文件名（由 LOG_* 宏传入）
 * @param line   源文件行号（由 LOG_* 宏传入）
 * @param func   函数名（由 LOG_* 宏传入）
 * @param fmt    格式化字符串
 * @param ...    可变参数
 * @return       无返回值
 * @note 线程安全；在持有互斥锁的状态下执行等级过滤与输出
 * @note 时间戳提取在锁外完成，避免在临界区内执行系统调用
 */
void log_write_impl(log_level_t level, const char* file, int line,
                    const char* func, const char* fmt, ...)
{
    /* 时间戳（锁外构造，避免在临界区内执行系统调用） */
    char time_buf[32];
    _make_timestamp(time_buf, sizeof(time_buf));

    /* 从完整路径中提取文件名（仅取最后一段） */
    const char* fname = strrchr(file, '/');
    fname = fname ? fname + 1 : file;

    hw_err_t ret = hw_mutex_lock(&g_log.lock);
    if (ret != HW_OK) {
        fprintf(stderr, "log: hw_mutex_lock failed in log_write_impl\n");
        return;
    }

    /* 在锁内做权威检查：初始化状态、文件句柄、等级过滤 */
    if (!g_log.initialized || !g_log.fp || level < g_log.min_level) {
        hw_mutex_unlock(&g_log.lock);
        return;
    }

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
    if (fprintf(g_log.fp, "%s%s\n", header, msg) < 0) {
        fprintf(stderr, "log: write to log file failed\n");
    }
    if (fprintf(stdout, "%s%s\n", header, msg) < 0) {
        fprintf(stderr, "log: write to stdout failed\n");
    }

    ret = hw_mutex_unlock(&g_log.lock);
    if (ret != HW_OK) {
        fprintf(stderr, "log: hw_mutex_unlock failed in log_write_impl\n");
    }
}
