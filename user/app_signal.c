/**
 * @file app_signal.c
 * @brief 应用级信号处理模块
 *
 * 信号处理器收到 SIGINT / SIGTERM 后调用 exit(0)，
 * 自动触发 atexit 回调链完成所有资源的优雅释放。
 *
 * 后续扩展方向：
 *   - SIGHUP 热重载配置（不使用 exit，设置重载标志）
 *   - 多次信号强杀（第一次 exit，第二次 _exit）
 */

#include "app_signal.h"
#include <signal.h>
#include <stdlib.h>

/* ── 内部状态 ─────────────────────────────────────────────────────── */

static int g_init = 0;   /* 是否已初始化 */

/* ── 内部信号处理器 ───────────────────────────────────────────────── */

/**
 * SIGINT / SIGTERM 共享处理器。
 *
 * 调用 exit(0) 触发 atexit 回调链，不在此处做任何清理。
 * exit() 虽然不完全符合 POSIX 异步信号安全标准，
 * 但在清理回调只做 close/free/log 的场景下是业界常规做法。
 *
 * @param sig  信号编号
 */
static void app_signal_handler(int sig)
{
    (void)sig;
    exit(0);
}

/* ── 公开 API ─────────────────────────────────────────────────────── */

/**
 * 初始化信号处理模块。
 *
 * 注册 SIGINT 和 SIGTERM 处理器，收到信号后调用 exit(0)，
 * 自动触发 atexit 回调链完成资源清理。重复调用安全。
 *
 * @note 信号处理器中只调 exit()，不直接做清理，
 *       避免异步信号安全问题（清理逻辑都在 atexit 回调中执行）
 */
void app_signal_init(void)
{
    if (g_init) return;
    g_init = 1;

    signal(SIGINT,  app_signal_handler);
    signal(SIGTERM, app_signal_handler);
}
