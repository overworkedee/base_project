#ifndef CMD_DISPATCHER_H
#define CMD_DISPATCHER_H

#include "cmd/cmd_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 前向声明 */
typedef struct cmd_conn     cmd_conn_t;
typedef struct cmd_dispatcher cmd_dispatcher_t;
typedef struct cmd_subscription_mgr cmd_subscription_mgr_t;

/**
 * 命令处理函数类型。
 *
 * @param req   请求帧（只读）
 * @param conn  来源连接
 * @param ctx   注册时传入的用户上下文
 */
typedef void (*cmd_handler_fn)(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx);

/**
 * 创建命令调度器。
 *
 * @param sub_mgr  订阅管理器（供 handler 使用），可为 NULL
 * @return         成功返回实例指针，失败返回 NULL
 */
cmd_dispatcher_t* cmd_dispatcher_create(cmd_subscription_mgr_t* sub_mgr);

/**
 * 销毁调度器。
 *
 * @param d  调度器实例
 */
void cmd_dispatcher_destroy(cmd_dispatcher_t* d);

/**
 * 注册命令处理函数及其上下文。
 *
 * 每个 CMD 大类可注册一个 handler 和独立的 ctx 指针。
 * 扩展新功能时只需调用此函数一次，无需修改任何现有代码。
 *
 * @param d       调度器实例
 * @param cmd     命令大类（0x01 ~ 0xFE）
 * @param handler 处理函数
 * @param ctx     传递给 handler 的用户上下文（可为 NULL）
 * @return        0 成功，-1 已注册
 */
int cmd_dispatcher_register(cmd_dispatcher_t* d, uint8_t cmd,
                            cmd_handler_fn handler, void* ctx);

/**
 * 分发请求帧到对应 handler。
 *
 * 若 CMD 未注册 handler，自动通过 cmd_conn_send 返回错误响应（CMD_ERR_UNKNOWN_CMD）。
 * handler 收到其注册时的 ctx 指针。
 *
 * @param d     调度器实例
 * @param req   请求帧
 * @param conn  来源连接
 */
void cmd_dispatcher_dispatch(cmd_dispatcher_t* d, const cmd_frame_t* req,
                             cmd_conn_t* conn);

/**
 * 获取调度器的订阅管理器。
 *
 * @param d  调度器实例
 * @return   订阅管理器指针（可为 NULL）
 */
cmd_subscription_mgr_t* cmd_dispatcher_get_sub_mgr(cmd_dispatcher_t* d);

#ifdef __cplusplus
}
#endif

#endif /* CMD_DISPATCHER_H */
