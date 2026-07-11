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

/* ── 日志订阅回调 ────────────────────────────────────────────────── */

/**
 * 日志订阅回调函数类型。每条通过 LOG_* 宏写入的日志会调用此回调。
 *
 * @param level  日志等级
 * @param msg    格式化后的完整日志消息
 * @param ctx    用户上下文
 * @note         回调在持有日志锁的情况下调用，回调内不应再调用 LOG_* 宏
 */
typedef void (*log_subscribe_fn_t)(uint8_t level, const char* msg, void* ctx);

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
 *
 * @note 先持有锁再执行清理，与 log_write_impl 互斥
 */
void log_deinit(void);

/**
 * 设置日志订阅回调。
 *
 * 设置后，每条通过 LOG_* 宏写入的日志都会通过回调推送。
 * 传 cb=NULL 取消回调。
 *
 * @param cb   回调函数指针
 * @param ctx  用户上下文，透传给回调
 */
void log_set_subscribe_callback(log_subscribe_fn_t cb, void* ctx);

/* ── 环形缓冲区 ──────────────────────────────────────────────────── */

#define LOG_RING_SIZE  2048  /* 环形缓冲区容量（条数）               */
#define LOG_MSG_MAX    256   /* 单条日志消息最大长度                 */

/**
 * 环形缓冲区条目。
 */
typedef struct {
    uint8_t  level;
    uint8_t  reserved;
    uint32_t timestamp;     /* unix timestamp，LE */
    char     msg[LOG_MSG_MAX];
} log_ring_entry_t;

/**
 * 从环形缓冲区获取所有已缓存的日志条目。
 *
 * 条目按时间顺序（从旧到新）排列。
 *
 * @param out_entries  输出缓冲区（调用方分配）
 * @param out_count    输出实际条目数
 */
void log_ring_get_all(log_ring_entry_t* out_entries, int* out_count);

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
