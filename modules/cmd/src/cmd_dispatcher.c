/**
 * cmd_dispatcher.c -- 命令调度实现
 *
 * 内部持有 CMD → handler 的路由表，将解析后的请求帧路由到对应的命令处理器。
 * 自动处理未知命令和未知子命令的错误响应。
 */

#define _GNU_SOURCE
#include "cmd/cmd_dispatcher.h"
#include "cmd/cmd_server.h"
#include "cmd/cmd_subscription.h"
#include "cmd/cmd_protocol.h"
#include "log/log.h"

#include <stdlib.h>
#include <string.h>

#define MAX_HANDLERS 256  /* CMD 是 1 字节 */

typedef struct cmd_dispatcher {
    cmd_handler_fn          handlers[MAX_HANDLERS];
    cmd_subscription_mgr_t* sub_mgr;
    void*                   ctx;
} cmd_dispatcher_t;

/* ── 发送错误响应的内部辅助 ─────────────────────────────────────────── */

/**
 * 内部：向 conn 发送一个错误响应帧。
 *
 * @param conn       目标连接
 * @param req_cmd    原始请求的 CMD
 * @param req_sub    原始请求的 SUB
 * @param error_code 错误码
 */
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

cmd_dispatcher_t* cmd_dispatcher_create(cmd_subscription_mgr_t* sub_mgr, void* ctx)
{
    cmd_dispatcher_t* d = (cmd_dispatcher_t*)calloc(1, sizeof(cmd_dispatcher_t));
    if (d) {
        d->sub_mgr = sub_mgr;
        d->ctx     = ctx;
    }
    return d;
}

void cmd_dispatcher_destroy(cmd_dispatcher_t* d)
{
    free(d);
}

int cmd_dispatcher_register(cmd_dispatcher_t* d, uint8_t cmd, cmd_handler_fn handler)
{
    if (!d || !handler) return -1;
    if (d->handlers[cmd]) return -1;  /* 已注册 */

    d->handlers[cmd] = handler;
    LOG_DEBUG("Dispatcher: registered handler for CMD=0x%02X", cmd);
    return 0;
}

void cmd_dispatcher_dispatch(cmd_dispatcher_t* d, const cmd_frame_t* req,
                             cmd_conn_t* conn)
{
    if (!d || !req || !conn) return;

    uint8_t cmd = req->cmd;
    cmd_handler_fn handler = d->handlers[cmd];

    if (!handler) {
        LOG_DEBUG("Dispatcher: unknown CMD=0x%02X from fd=%d", cmd, conn->fd);
        _send_error(conn, cmd, req->sub, CMD_ERR_UNKNOWN_CMD);
        return;
    }

    handler(req, conn, d->ctx);
}

void* cmd_dispatcher_get_ctx(cmd_dispatcher_t* d)
{
    return d ? d->ctx : NULL;
}

cmd_subscription_mgr_t* cmd_dispatcher_get_sub_mgr(cmd_dispatcher_t* d)
{
    return d ? d->sub_mgr : NULL;
}
