/**
 * app_cmd.c — 应用级命令模块封装
 *
 * 将 cmd 模块（server/dispatcher/subscription/handler）的初始化、
 * 配置和生命周期管理封装在一个不透明结构体中，对 main.c 暴露简洁接口。
 */

#include "app_cmd.h"
#include "cmd/cmd_server.h"
#include "cmd/cmd_dispatcher.h"
#include "cmd/cmd_subscription.h"
#include "cmd/cmd_transport.h"
#include "cmd/cmd_handler_ctx.h"
#include "log/log.h"

#include <stdlib.h>
#include <string.h>

/* ── 前向声明 handler ───────────────────────────────────────────────── */

extern void cmd_handler_led(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx);
extern void cmd_handler_sensor(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx);
extern void cmd_handler_system(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx);

/* ── 内部结构 ───────────────────────────────────────────────────────── */

struct app_cmd {
    cmd_server_t*          server;
    cmd_dispatcher_t*      dispatcher;
    cmd_subscription_mgr_t* sub_mgr;
    cmd_handler_ctx_t      hctx;
    int                    listener_count;
};

/* ── 服务器回调（桥接 server → dispatcher）─────────────────────────── */

static void on_request(const cmd_frame_t* req, cmd_conn_t* conn)
{
    /* 从 conn 上下文取 dispatcher（由 app_cmd_create 注入） */
    cmd_dispatcher_t* disp = (cmd_dispatcher_t*)cmd_conn_get_ctx(conn);
    if (disp) {
        cmd_dispatcher_dispatch(disp, req, conn);
    }
}

/* ── 公开 API ───────────────────────────────────────────────────────── */

app_cmd_t* app_cmd_create(led_t* led, sht30_t* sht30)
{
    app_cmd_t* cmd = (app_cmd_t*)calloc(1, sizeof(app_cmd_t));
    if (!cmd) return NULL;

    /* 订阅管理器 */
    cmd->sub_mgr = cmd_subscription_create();
    if (!cmd->sub_mgr) { free(cmd); return NULL; }

    /* handler 上下文 */
    memset(&cmd->hctx, 0, sizeof(cmd->hctx));
    cmd->hctx.led     = led;
    cmd->hctx.sht30   = sht30;
    cmd->hctx.sub_mgr = cmd->sub_mgr;

    /* 调度器 */
    cmd->dispatcher = cmd_dispatcher_create(cmd->sub_mgr, &cmd->hctx);
    if (!cmd->dispatcher) { cmd_subscription_destroy(cmd->sub_mgr); free(cmd); return NULL; }

    cmd_dispatcher_register(cmd->dispatcher, CMD_LED,    cmd_handler_led);
    cmd_dispatcher_register(cmd->dispatcher, CMD_SENSOR, cmd_handler_sensor);
    cmd_dispatcher_register(cmd->dispatcher, CMD_SYSTEM, cmd_handler_system);

    /* 命令服务器 */
    cmd->server = cmd_server_create();
    if (!cmd->server) {
        cmd_dispatcher_destroy(cmd->dispatcher);
        cmd_subscription_destroy(cmd->sub_mgr);
        free(cmd);
        return NULL;
    }
    cmd_server_set_handler(cmd->server, on_request);

    LOG_INFO("Command module initialized");
    return cmd;
}

void app_cmd_destroy(app_cmd_t* cmd)
{
    if (!cmd) return;

    if (cmd->server) {
        cmd_server_stop(cmd->server);
        cmd_server_destroy(cmd->server);
        cmd->server = NULL;
    }

    if (cmd->dispatcher) {
        cmd_dispatcher_destroy(cmd->dispatcher);
        cmd->dispatcher = NULL;
    }

    if (cmd->sub_mgr) {
        cmd_subscription_destroy(cmd->sub_mgr);
        cmd->sub_mgr = NULL;
    }

    free(cmd);
    LOG_INFO("Command module destroyed");
}

int app_cmd_add_listener_unix(app_cmd_t* cmd, const char* path)
{
    if (!cmd || !path) return -1;

    int fd = cmd_transport_listen_unix(path);
    if (fd < 0) return -1;

    if (cmd_server_add_listener(cmd->server, fd) != 0) {
        cmd_transport_close(fd);
        return -1;
    }

    cmd->listener_count++;
    return 0;
}

int app_cmd_add_listener_tcp(app_cmd_t* cmd, uint16_t port)
{
    if (!cmd) return -1;

    int fd = cmd_transport_listen_tcp(port);
    if (fd < 0) return -1;

    if (cmd_server_add_listener(cmd->server, fd) != 0) {
        cmd_transport_close(fd);
        return -1;
    }

    cmd->listener_count++;
    return 0;
}

int app_cmd_run(app_cmd_t* cmd)
{
    if (!cmd || !cmd->server || cmd->listener_count == 0) {
        LOG_ERROR("app_cmd_run: no listeners configured");
        return -1;
    }

    LOG_INFO("Entering command server event loop...");
    return cmd_server_run(cmd->server);
}

void app_cmd_stop(app_cmd_t* cmd)
{
    if (cmd && cmd->server) {
        cmd_server_stop(cmd->server);
    }
}

/**
 * 获取订阅管理器指针（供传感器线程使用）。
 *
 * @param cmd  命令模块实例
 * @return     订阅管理器指针，NULL 表示未初始化
 */
cmd_subscription_mgr_t* app_cmd_get_sub_mgr(app_cmd_t* cmd)
{
    return cmd ? cmd->sub_mgr : NULL;
}
