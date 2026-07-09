/**
 * app_cmd.c — 应用级命令模块封装
 *
 * 将 cmd 模块（server/dispatcher/subscription）的初始化、
 * 配置和生命周期管理封装在一个不透明结构体中，对 main.c 暴露简洁接口。
 *
 * 采用注册模式：通过 app_cmd_register 独立注册每个 CMD 大类
 * 的 handler 和上下文，扩展新功能无需修改本文件。
 */

#include "app_cmd.h"
#include "cmd/cmd_server.h"
#include "cmd/cmd_subscription.h"
#include "cmd/cmd_transport.h"
#include "log/log.h"

#include <stdlib.h>

/* ── 内部结构 ───────────────────────────────────────────────────────── */

struct app_cmd {
    cmd_server_t*           server;
    cmd_dispatcher_t*       dispatcher;
    cmd_subscription_mgr_t* sub_mgr;
    int                     listener_count;
};

/* ── 服务器回调（桥接 server → dispatcher）─────────────────────────── */

/**
 * 服务器收到完整帧时的回调。user_data 指向 dispatcher。
 */
static void on_request(const cmd_frame_t* req, cmd_conn_t* conn,
                       void* user_data)
{
    cmd_dispatcher_t* disp = (cmd_dispatcher_t*)user_data;
    cmd_dispatcher_dispatch(disp, req, conn);
}

/* ── 公开 API ───────────────────────────────────────────────────────── */

app_cmd_t* app_cmd_create(void)
{
    app_cmd_t* cmd = (app_cmd_t*)calloc(1, sizeof(app_cmd_t));
    if (!cmd) return NULL;

    /* 订阅管理器（sensor handler 需要） */
    cmd->sub_mgr = cmd_subscription_create();
    if (!cmd->sub_mgr) { free(cmd); return NULL; }

    /* 调度器 */
    cmd->dispatcher = cmd_dispatcher_create(cmd->sub_mgr);
    if (!cmd->dispatcher) {
        cmd_subscription_destroy(cmd->sub_mgr);
        free(cmd);
        return NULL;
    }

    /* 命令服务器 */
    cmd->server = cmd_server_create();
    if (!cmd->server) {
        cmd_dispatcher_destroy(cmd->dispatcher);
        cmd_subscription_destroy(cmd->sub_mgr);
        free(cmd);
        return NULL;
    }
    cmd_server_set_handler(cmd->server, on_request, cmd->dispatcher);

    LOG_INFO("Command module initialized");
    return cmd;
}

void app_cmd_destroy(app_cmd_t* cmd)
{
    if (!cmd) return;

    if (cmd->server) {
        cmd_server_stop(cmd->server);
        cmd_server_destroy(cmd->server);
    }

    if (cmd->dispatcher) {
        cmd_dispatcher_destroy(cmd->dispatcher);
    }

    if (cmd->sub_mgr) {
        cmd_subscription_destroy(cmd->sub_mgr);
    }

    free(cmd);
    LOG_INFO("Command module destroyed");
}

int app_cmd_register(app_cmd_t* cmd, uint8_t cmd_cls,
                     cmd_handler_fn handler, void* ctx)
{
    if (!cmd || !cmd->dispatcher) return -1;
    return cmd_dispatcher_register(cmd->dispatcher, cmd_cls, handler, ctx);
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

cmd_subscription_mgr_t* app_cmd_get_sub_mgr(app_cmd_t* cmd)
{
    return cmd ? cmd->sub_mgr : NULL;
}
