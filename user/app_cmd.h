#ifndef APP_CMD_H
#define APP_CMD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 前向声明 */
typedef struct led_ctx              led_t;
typedef struct sht30_ctx            sht30_t;
typedef struct app_cmd              app_cmd_t;
typedef struct cmd_subscription_mgr cmd_subscription_mgr_t;

/**
 * 创建命令模块实例。
 *
 * 内部初始化 dispatcher（注册 LED/Sensor/System 三个 handler）、
 * 订阅管理器、命令服务器。不在此处创建监听端口。
 *
 * @param led     LED 句柄（可为 NULL，此时 LED 命令返回 HW_ERR）
 * @param sht30   SHT30 传感器句柄（可为 NULL，此时传感器命令返回 HW_ERR）
 * @return        成功返回实例指针，失败返回 NULL
 */
app_cmd_t* app_cmd_create(led_t* led, sht30_t* sht30);

/**
 * 销毁命令模块实例并释放所有资源。
 *
 * 先 stop 再销毁。重复调用安全。
 *
 * @param cmd  命令模块实例（可为 NULL）
 */
void app_cmd_destroy(app_cmd_t* cmd);

/**
 * 添加 Unix Domain Socket 监听。
 *
 * 必须在 app_cmd_run 之前调用。
 *
 * @param cmd   命令模块实例
 * @param path  socket 文件路径，如 "/tmp/cmd.sock"
 * @return      0 成功，-1 失败
 */
int app_cmd_add_listener_unix(app_cmd_t* cmd, const char* path);

/**
 * 添加 TCP Socket 监听。
 *
 * 必须在 app_cmd_run 之前调用。
 *
 * @param cmd   命令模块实例
 * @param port  监听端口号
 * @return      0 成功，-1 失败
 */
int app_cmd_add_listener_tcp(app_cmd_t* cmd, uint16_t port);

/**
 * 启动命令服务器事件循环（阻塞当前线程）。
 *
 * 必须先调用 app_cmd_add_listener_* 至少一次。
 * 内部调用 cmd_server_run，阻塞直到 cmd_server_stop 被调用。
 *
 * @param cmd  命令模块实例
 * @return     0 正常退出，-1 错误
 */
int app_cmd_run(app_cmd_t* cmd);

/**
 * 请求命令服务器退出。
 *
 * 可从信号处理器或任意线程调用。
 *
 * @param cmd  命令模块实例
 */
void app_cmd_stop(app_cmd_t* cmd);

/**
 * 获取订阅管理器指针（供传感器线程推送数据）。
 *
 * @param cmd  命令模块实例
 * @return     订阅管理器指针，NULL 表示未初始化
 */
cmd_subscription_mgr_t* app_cmd_get_sub_mgr(app_cmd_t* cmd);

#ifdef __cplusplus
}
#endif

#endif /* APP_CMD_H */
