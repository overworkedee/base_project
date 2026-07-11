/**
 * cmd_dispatcher.c -- 命令调度实现
 *
 * 内部持有 CMD → (handler, ctx) 的路由表。
 * 每个 CMD 大类可独立注册 handler 和上下文，分发时自动匹配。
 */

#define _GNU_SOURCE
#include "cmd/cmd_dispatcher.h"
#include "cmd/cmd_server.h"
#include "cmd/cmd_protocol.h"
#include "log/log.h"

#include <stdlib.h>
#include <string.h>

#define MAX_HANDLERS 256  /* CMD 是 1 字节 */

typedef struct {
    cmd_handler_fn handler;
    void*          ctx;
} handler_entry_t;

typedef struct cmd_dispatcher {
    handler_entry_t         entries[MAX_HANDLERS];
    cmd_subscription_mgr_t* sub_mgr;
} cmd_dispatcher_t;

/* ── 发送错误响应的内部辅助 ─────────────────────────────────────────── */

static void _send_error(cmd_conn_t* conn, uint8_t req_cmd, uint8_t req_sub,
                        uint8_t error_code)
{
    uint8_t err_pld = error_code;
    cmd_frame_t rsp;
    rsp.cmd     = req_cmd;
    rsp.sub     = cmd_frame_sub_rsp(req_sub);
    rsp.len     = 1;
    rsp.payload = &err_pld;

    cmd_conn_send(conn, &rsp);
}

/* ── 公开 API ───────────────────────────────────────────────────────── */

/**
 * 创建命令调度器实例。
 *
 * @param sub_mgr  订阅管理器指针（可为 NULL，传入后可通过 get_sub_mgr 获取）
 * @return         成功返回实例，失败返回 NULL
 */
cmd_dispatcher_t* cmd_dispatcher_create(cmd_subscription_mgr_t* sub_mgr)
{
    cmd_dispatcher_t* d = (cmd_dispatcher_t*)calloc(1, sizeof(cmd_dispatcher_t));
    if (d) {
        d->sub_mgr = sub_mgr;
    }
    return d;
}

/**
 * 销毁调度器并释放内存。
 *
 * @param d  调度器实例
 */
void cmd_dispatcher_destroy(cmd_dispatcher_t* d)
{
    free(d);
}

/**
 * 注册命令处理函数及其上下文。
 *
 * 每个 CMD 大类最多注册一个 handler。扩展新功能只需调用此函数，
 * 无需修改 dispatcher 本身。
 *
 * @param d       调度器实例
 * @param cmd     命令大类（0x01~0xFF，直接作为数组索引）
 * @param handler 处理函数
 * @param ctx     用户上下文（透传给 handler，可为 NULL）
 * @return        0 成功，-1 参数无效或已注册
 */
int cmd_dispatcher_register(cmd_dispatcher_t* d, uint8_t cmd,
                            cmd_handler_fn handler, void* ctx)
{
    if (!d || !handler) return -1;
    if (d->entries[cmd].handler) return -1;  /* 已注册 */

    d->entries[cmd].handler = handler;
    d->entries[cmd].ctx     = ctx;
    LOG_DEBUG("Dispatcher: registered handler for CMD=0x%02X", cmd);
    return 0;
}

/**
 * 分发请求帧到对应的 handler。
 *
 * 按 req->cmd 查路由表，找到则调用 handler(req, conn, ctx)。
 * 未注册的 CMD 自动回复 CMD_ERR_UNKNOWN_CMD 错误帧。
 *
 * @param d     调度器实例
 * @param req   请求帧
 * @param conn  来源连接
 */
void cmd_dispatcher_dispatch(cmd_dispatcher_t* d, const cmd_frame_t* req,
                             cmd_conn_t* conn)
{
    if (!d || !req || !conn) return;

    uint8_t cmd = req->cmd;
    handler_entry_t* entry = &d->entries[cmd];

    if (!entry->handler) {
        LOG_DEBUG("Dispatcher: unknown CMD=0x%02X", cmd);
        _send_error(conn, cmd, req->sub, CMD_ERR_UNKNOWN_CMD);
        return;
    }

    entry->handler(req, conn, entry->ctx);
}

/**
 * 获取调度器内的订阅管理器指针。
 *
 * @param d  调度器实例
 * @return   订阅管理器指针（创建时传入，可为 NULL）
 */
cmd_subscription_mgr_t* cmd_dispatcher_get_sub_mgr(cmd_dispatcher_t* d)
{
    return d ? d->sub_mgr : NULL;
}
