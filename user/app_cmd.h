#ifndef APP_CMD_H
#define APP_CMD_H

#include <stdint.h>
#include "cmd/cmd_dispatcher.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 前向声明 */
typedef struct app_cmd              app_cmd_t;
typedef struct cmd_subscription_mgr cmd_subscription_mgr_t;

/**
 * 创建命令模块实例。
 *
 * 内部初始化 dispatcher、订阅管理器、命令服务器。
 * 调用 app_cmd_register 注册命令处理器，然后 app_cmd_run 启动事件循环。
 *
 * @return 成功返回实例指针，失败返回 NULL
 */
app_cmd_t* app_cmd_create(void);

/**
 * 销毁命令模块实例并释放所有资源。
 *
 * @param cmd  命令模块实例（可为 NULL）
 */
void app_cmd_destroy(app_cmd_t* cmd);

/**
 * 注册命令处理器。
 *
 * 扩展新功能时只需调用此函数一次，无需修改 app_cmd 或 main.c。
 * 每个 CMD 大类可以注册独立的 handler 和 ctx。
 *
 * @param cmd      命令模块实例
 * @param cmd_cls  命令大类（CMD_LED / CMD_SENSOR / CMD_SYSTEM / ...）
 * @param handler  处理函数
 * @param ctx      传递给 handler 的用户上下文（led_t* / sensor_ctx / ...）
 * @return         0 成功，-1 已注册或参数错误
 */
int app_cmd_register(app_cmd_t* cmd, uint8_t cmd_cls,
                     cmd_handler_fn handler, void* ctx);

/**
 * 添加 Unix Domain Socket 监听。
 */
int app_cmd_add_listener_unix(app_cmd_t* cmd, const char* path);

/**
 * 添加 TCP Socket 监听。
 */
int app_cmd_add_listener_tcp(app_cmd_t* cmd, uint16_t port);

/**
 * 启动命令服务器事件循环（阻塞当前线程）。
 */
int app_cmd_run(app_cmd_t* cmd);

/**
 * 请求命令服务器退出。
 */
void app_cmd_stop(app_cmd_t* cmd);

/**
 * 获取订阅管理器指针（供传感器线程推送数据）。
 */
cmd_subscription_mgr_t* app_cmd_get_sub_mgr(app_cmd_t* cmd);

#ifdef __cplusplus
}
#endif

#endif /* APP_CMD_H */
