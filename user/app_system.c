/**
 * app_system.c — 系统命令处理器
 *
 * 处理 CMD=0x03:
 *   SUB=0x02 (READ)              查询系统信息 → 返回版本字符串
 *   SUB=0x03 (SET_LOGLEVEL)      设置日志等级 → PAYLOAD=[level 1B] 0=DEBUG..3=ERROR
 *   SUB=0x05 (LOG_SUBSCRIBE)     订阅日志推送 → 先回放缓冲区(≤500条)再注册实时推送
 *   SUB=0x06 (LOG_UNSUBSCRIBE)   取消日志订阅
 *
 * 解耦设计（回调注入）：
 *   不直接持有 cmd_subscription_mgr_t*，
 *   订阅/取消订阅通过 app_cmd_svc_t 能力表（函数指针）完成。
 */

#define _GNU_SOURCE
#include "app_system.h"
#include "cmd/cmd_server.h"
#include "cmd/cmd_protocol.h"
#include "cmd/cmd_frame.h"
#include "log/log.h"

#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>  /* htons */

#define APP_VERSION  "1.0.0"

/* ── 内部结构 ───────────────────────────────────────────────────────── */

struct app_system {
    const app_cmd_svc_t* svc;   /* 命令服务能力表（回调注入） */
};

/* ── 命令处理器 ─────────────────────────────────────────────────────── */

/**
 * 系统命令处理器（CMD=0x03）。
 *
 * ctx 应为 app_system_t*，内部通过 svc 能力表处理日志订阅。
 *
 * @param req   请求帧
 * @param conn  来源连接
 * @param ctx   app_system_t* 句柄
 */
static void cmd_handler_system(const cmd_frame_t* req, cmd_conn_t* conn,
                               void* ctx)
{
    app_system_t* system = (app_system_t*)ctx;
    const app_cmd_svc_t* svc = system ? system->svc : NULL;

    uint8_t op = cmd_frame_sub_req(req->sub);

    if (op == CMD_SUB_READ) {
        /* 查询系统信息: 返回版本号 */
        const char* ver = APP_VERSION;
        size_t ver_len = strlen(ver);

        /* PAYLOAD = [err 1B, version_str N B] */
        uint8_t* pld = (uint8_t*)malloc(1 + ver_len);
        if (!pld) {
            uint8_t err = CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_SYSTEM, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        pld[0] = CMD_ERR_OK;
        memcpy(pld + 1, ver, ver_len);

        cmd_frame_t rsp;
        rsp.cmd     = CMD_SYSTEM;
        rsp.sub     = cmd_frame_sub_rsp(req->sub);
        rsp.len     = (uint16_t)(1 + ver_len);
        rsp.payload = pld;

        cmd_conn_send(conn, &rsp);
        free(pld);

        LOG_DEBUG("System info requested");

    } else if (op == 0x03) {
        /* 设置日志等级: PAYLOAD=[level 1B] */
        if (req->len < 1) {
            uint8_t err = CMD_ERR_PARAM;
            cmd_frame_t rsp = { .cmd = CMD_SYSTEM, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        uint8_t level = req->payload[0];
        if (level > LOG_ERROR) {
            uint8_t err = CMD_ERR_PARAM;
            cmd_frame_t rsp = { .cmd = CMD_SYSTEM, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        log_set_level((log_level_t)level);

        uint8_t rsp_pld[2];
        rsp_pld[0] = CMD_ERR_OK;
        rsp_pld[1] = level;

        cmd_frame_t rsp = { .cmd = CMD_SYSTEM, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 2, .payload = rsp_pld };
        cmd_conn_send(conn, &rsp);

        LOG_INFO("Log level changed to %d by command", level);

    } else if (op == CMD_SUB_LOG_SUBSCRIBE) {
        /* 订阅日志推送: 先推送缓冲日志(最多500条)，再注册实时推送 */
        if (!svc || !svc->subscribe_data) {
            uint8_t err = CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_SYSTEM, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        /* 通过能力表注册订阅 */
        svc->subscribe_data(svc->owner, CMD_DATA_LOG, 0, conn);

        /* 推送已缓存的日志（最多 500 条） */
        log_ring_entry_t entries[500];
        int count = 0;
        log_ring_get_all(entries, &count);
        if (count > 500) count = 500;

        for (int i = 0; i < count; i++) {
            /* 构造推送帧: [data_id 2B BE, level 1B, reserved 1B, timestamp 4B LE, msg N B] */
            size_t msg_len = strlen(entries[i].msg);
            size_t pld_len = 2 + 1 + 1 + 4 + msg_len;
            uint8_t* pld = (uint8_t*)malloc(pld_len);
            if (!pld) continue;

            uint16_t id_be = htons(CMD_DATA_LOG);
            uint8_t* p = pld;
            memcpy(p, &id_be, 2);       p += 2;
            *p++ = entries[i].level;
            *p++ = entries[i].reserved;
            memcpy(p, &entries[i].timestamp, 4);  p += 4;
            memcpy(p, entries[i].msg, msg_len);

            cmd_frame_t push;
            push.cmd     = CMD_SYSTEM;
            push.sub     = cmd_frame_sub_rsp(CMD_SUB_LOG_SUBSCRIBE);
            push.len     = (uint16_t)pld_len;
            push.payload = pld;

            cmd_conn_send(conn, &push);
            free(pld);
        }

        /* 回复确认 */
        uint8_t ok = CMD_ERR_OK;
        cmd_frame_t ack = { .cmd = CMD_SYSTEM, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 1, .payload = &ok };
        cmd_conn_send(conn, &ack);

        LOG_DEBUG("Log subscription added, pushed %d buffered entries", count);

    } else if (op == CMD_SUB_LOG_UNSUBSCRIBE) {
        /* 取消日志订阅 */
        if (svc && svc->unsubscribe_data) {
            svc->unsubscribe_data(svc->owner, CMD_DATA_LOG, conn);
        }

        uint8_t ok = CMD_ERR_OK;
        cmd_frame_t ack = { .cmd = CMD_SYSTEM, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 1, .payload = &ok };
        cmd_conn_send(conn, &ack);

        LOG_DEBUG("Log subscription removed");

    } else {
        uint8_t err = CMD_ERR_UNKNOWN_SUB;
        cmd_frame_t rsp = { .cmd = CMD_SYSTEM, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 1, .payload = &err };
        cmd_conn_send(conn, &rsp);
    }
}

/* ── 生命周期 ───────────────────────────────────────────────────────── */

/**
 * 创建系统应用模块并注册 CMD_SYSTEM 处理器。
 *
 * 内部通过 svc->register_cmd 将 cmd_handler_system 注入调度器。
 *
 * @param svc  命令服务能力表（来自 app_cmd_get_svc），可为 NULL
 * @return     成功返回实例指针，失败返回 NULL
 */
app_system_t* app_system_create(const app_cmd_svc_t* svc)
{
    app_system_t* system = (app_system_t*)calloc(1, sizeof(app_system_t));
    if (!system) return NULL;

    system->svc = svc;

    if (svc && svc->register_cmd) {
        svc->register_cmd(svc->owner, CMD_SYSTEM, cmd_handler_system, system);
    }

    return system;
}

/**
 * 释放系统应用模块。
 *
 * @param system  系统应用模块（可为 NULL）
 */
void app_system_destroy(app_system_t* system)
{
    if (!system) return;
    free(system);
}