/**
 * app_cmd.c — 应用级命令模块封装
 *
 * 将 cmd 模块（server/dispatcher/subscription）的初始化、
 * 配置和生命周期管理封装在不透明结构体中，对上层暴露简洁接口。
 *
 * 解耦设计（回调注入）：
 *   通过 app_cmd_get_svc 返回能力表（函数指针集合），
 *   各 app 模块只依赖 app_registry.h 中的抽象回调，
 *   不直接触碰 cmd 内部类型（server/dispatcher/sub_mgr）。
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
    app_cmd_svc_t           svc;    /* 对外能力表 */
};

/* ── 服务器回调（桥接 server → dispatcher）─────────────────────────── */

/**
 * 服务器收到完整帧时的回调。user_data 指向 dispatcher。
 *
 * @param req        请求帧
 * @param conn       来源连接
 * @param user_data  cmd_dispatcher_t*
 */
static void on_request(const cmd_frame_t* req, cmd_conn_t* conn,
                       void* user_data)
{
    cmd_dispatcher_t* disp = (cmd_dispatcher_t*)user_data;
    cmd_dispatcher_dispatch(disp, req, conn);
}

/* ── 能力表实现（供各 app 模块回调注入）────────────────────────────── */

/**
 * 注册命令 handler（能力表 register_cmd 实现）。
 *
 * @param owner     app_cmd_t* 实例
 * @param cmd_cls   命令大类
 * @param handler   处理函数
 * @param ctx       用户上下文
 * @return          0 成功，-1 失败
 */
static int svc_register_cmd(void* owner, uint8_t cmd_cls,
                            cmd_handler_fn handler, void* ctx)
{
    app_cmd_t* self = (app_cmd_t*)owner;
    if (!self || !self->dispatcher) return -1;
    return cmd_dispatcher_register(self->dispatcher, cmd_cls, handler, ctx);
}

/**
 * 推送数据流（能力表 publish_data 实现）。
 *
 * @param owner      app_cmd_t* 实例
 * @param cmd        命令大类
 * @param data_id    数据流 ID
 * @param value      数据值（大端序）
 * @param value_len  数据长度
 * @return           成功推送的连接数，-1 失败
 */
static int svc_publish_data(void* owner, uint8_t cmd, uint16_t data_id,
                            const uint8_t* value, size_t value_len)
{
    app_cmd_t* self = (app_cmd_t*)owner;
    if (!self || !self->sub_mgr) return -1;
    return cmd_subscription_push(self->sub_mgr, cmd, data_id, value, value_len);
}

/**
 * 添加订阅（能力表 subscribe_data 实现）。
 *
 * @param owner        app_cmd_t* 实例
 * @param data_id      数据流 ID
 * @param interval_ms  推送间隔（毫秒）
 * @param conn         订阅者连接
 * @return             0 成功，-1 失败
 */
static int svc_subscribe_data(void* owner, uint16_t data_id,
                              uint32_t interval_ms, cmd_conn_t* conn)
{
    app_cmd_t* self = (app_cmd_t*)owner;
    if (!self || !self->sub_mgr) return -1;
    return cmd_subscription_add(self->sub_mgr, data_id, interval_ms, conn);
}

/**
 * 移除订阅（能力表 unsubscribe_data 实现）。
 *
 * @param owner     app_cmd_t* 实例
 * @param data_id   数据流 ID
 * @param conn      订阅者连接
 * @return          0 成功，-1 未找到
 */
static int svc_unsubscribe_data(void* owner, uint16_t data_id,
                                cmd_conn_t* conn)
{
    app_cmd_t* self = (app_cmd_t*)owner;
    if (!self || !self->sub_mgr) return -1;
    return cmd_subscription_remove(self->sub_mgr, data_id, conn);
}

/* ── 公开 API ───────────────────────────────────────────────────────── */

/**
 * 创建命令模块实例。
 *
 * 内部依次创建订阅管理器、调度器、命令服务器，桥接回调，
 * 并组装对外能力表（app_cmd_svc_t）。
 *
 * @return 成功返回实例指针，失败返回 NULL
 */
app_cmd_t* app_cmd_create(void)
{
    app_cmd_t* cmd = (app_cmd_t*)calloc(1, sizeof(app_cmd_t));
    if (!cmd) return NULL;

    /* 订阅管理器 */
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

    /* 组装能力表 */
    cmd->svc.owner            = cmd;
    cmd->svc.register_cmd     = svc_register_cmd;
    cmd->svc.publish_data     = svc_publish_data;
    cmd->svc.subscribe_data   = svc_subscribe_data;
    cmd->svc.unsubscribe_data = svc_unsubscribe_data;

    LOG_INFO("Command module initialized");
    return cmd;
}

/**
 * 销毁命令模块并释放所有资源。
 *
 * @param cmd  命令模块实例（可为 NULL，此时无操作）
 */
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

/**
 * 获取命令服务能力表（handler 注册 / 数据推送 / 订阅）。
 *
 * 各 app 模块通过此表注入回调，避免直接依赖 cmd 内部类型。
 *
 * @param cmd  命令模块实例
 * @return     能力表指针（随 app_cmd_t 生命周期有效），cmd 为 NULL 时返回 NULL
 */
const app_cmd_svc_t* app_cmd_get_svc(app_cmd_t* cmd)
{
    return cmd ? &cmd->svc : NULL;
}

/**
 * 添加 Unix Domain Socket 监听。
 *
 * @param cmd   命令模块实例
 * @param path  socket 路径（如 CMD_DEFAULT_UNIX_SOCK_PATH）
 * @return      0 成功，-1 失败（路径不可用或已存在）
 */
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

/**
 * 添加 TCP 监听。
 *
 * @param cmd   命令模块实例
 * @param port  监听端口（如 9527）
 * @return      0 成功，-1 失败（端口被占用等）
 */
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

/**
 * 启动命令服务器事件循环（阻塞当前线程）。
 *
 * @param cmd  命令模块实例
 * @return     0 正常退出，-1 失败（无监听器）
 */
int app_cmd_run(app_cmd_t* cmd)
{
    if (!cmd || !cmd->server || cmd->listener_count == 0) {
        LOG_ERROR("app_cmd_run: no listeners configured");
        return -1;
    }

    LOG_INFO("Entering command server event loop...");
    return cmd_server_run(cmd->server);
}

/**
 * 请求命令服务器退出（非阻塞）。
 *
 * @param cmd  命令模块实例（可为 NULL）
 */
void app_cmd_stop(app_cmd_t* cmd)
{
    if (cmd && cmd->server) {
        cmd_server_stop(cmd->server);
    }
}