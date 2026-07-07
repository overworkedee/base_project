#ifndef APP_SIGNAL_H
#define APP_SIGNAL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化信号处理模块。
 *
 * 注册 SIGINT（Ctrl+C）和 SIGTERM（kill 默认）的处理器。
 * 收到信号时处理器调用 exit(0)，自动触发 atexit 回调链完成资源清理。
 *
 * 用法：
 *   app_signal_init();          // 注册信号 → exit
 *   atexit(my_cleanup);         // 注册清理回调（可多次调用，后进先出）
 *   // ... 初始化设备，进入主循环 ...
 *   // Ctrl+C → exit(0) → 依次调用所有 atexit 回调 → 进程退出
 *
 * @note 信号处理器中只调 exit()，不直接做清理，避免异步信号安全问题。
 *       所有清理逻辑通过 atexit() 在正常上下文中执行。
 * @note 重复调用安全（幂等）。
 */
void app_signal_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SIGNAL_H */
