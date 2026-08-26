#ifndef APP_REGISTRY_H
#define APP_REGISTRY_H

#include <stdint.h>
#include <stddef.h>
#include "cmd/cmd_dispatcher.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 回调节点类型（能力声明，具体实现由注册者注入） ──────────────── */

/**
 * 命令 handler 注册回调。
 * 由 cmd 服务实现：把 (cmd_cls → handler, ctx) 登记进调度器。
 *
 * @param owner      服务实现实例（app_cmd_t*）
 * @param cmd_cls    命令大类（CMD_LED / CMD_SENSOR / ...）
 * @param handler    处理函数
 * @param ctx        传给 handler 的用户上下文
 * @return           0 成功，-1 失败（重复注册等）
 */
typedef int (*app_cmd_register_fn)(void* owner, uint8_t cmd_cls,
                                   cmd_handler_fn handler, void* ctx);

/**
 * 数据流发布回调。
 * 由 cmd 服务实现：向所有订阅 data_id 的连接推送数据帧。
 *
 * @param owner       服务实现实例（app_cmd_t*）
 * @param cmd         命令大类（组帧用，通常 CMD_SENSOR）
 * @param data_id     数据流 ID（CMD_DATA_*）
 * @param value       数据值（已为大端序）
 * @param value_len   数据长度（字节）
 * @return            成功推送的连接数，-1 失败
 */
typedef int (*app_publish_fn)(void* owner, uint8_t cmd, uint16_t data_id,
                              const uint8_t* value, size_t value_len);

/**
 * 订阅回调。
 * 由 cmd 服务实现：把 (data_id, interval, conn) 登记进订阅管理器。
 *
 * @param owner        服务实现实例（app_cmd_t*）
 * @param data_id      数据流 ID
 * @param interval_ms  推送间隔（毫秒），0 表示事件驱动
 * @param conn         订阅者连接
 * @return             0 成功，-1 失败
 */
typedef int (*app_subscribe_fn)(void* owner, uint16_t data_id,
                                uint32_t interval_ms, cmd_conn_t* conn);

/**
 * 取消订阅回调。
 * 由 cmd 服务实现：移除 (data_id, conn) 订阅。
 *
 * @param owner     服务实现实例（app_cmd_t*）
 * @param data_id   数据流 ID
 * @param conn      订阅者连接
 * @return          0 成功，-1 未找到
 */
typedef int (*app_unsubscribe_fn)(void* owner, uint16_t data_id,
                                  cmd_conn_t* conn);

/* ── 命令服务能力表（由 app_cmd 实现并对外提供） ─────────────────── */

typedef struct {
    void*                owner;            /* 能力实现实例（app_cmd_t*） */
    app_cmd_register_fn  register_cmd;     /* 注册命令 handler          */
    app_publish_fn       publish_data;     /* 向订阅者推送数据流        */
    app_subscribe_fn     subscribe_data;   /* 添加订阅                  */
    app_unsubscribe_fn   unsubscribe_data; /* 移除订阅                  */
} app_cmd_svc_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_REGISTRY_H */