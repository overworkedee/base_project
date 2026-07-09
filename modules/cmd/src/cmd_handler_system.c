/**
 * cmd_handler_system.c -- 系统命令处理器
 *
 * 处理 CMD=0x03:
 *   SUB=0x02 查询系统信息: 返回版本字符串
 *   SUB=0x03 设置日志等级: PAYLOAD=[level 1B] 0=DEBUG 1=INFO 2=WARN 3=ERROR
 */

#define _GNU_SOURCE
#include "cmd/cmd_dispatcher.h"
#include "cmd/cmd_server.h"
#include "cmd/cmd_protocol.h"
#include "log/log.h"

#include <stdlib.h>
#include <string.h>

#define APP_VERSION  "1.0.0"

void cmd_handler_system(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx)
{
    (void)ctx;  /* system handler 不需要 ctx */

    uint8_t op = cmd_frame_sub_req(req->sub);

    if (op == CMD_SUB_READ) {
        /* 查询系统信息: 返回版本号 */
        const char* ver = APP_VERSION;
        size_t ver_len = strlen(ver);

        /* PAYLOAD = [err 1B, version_str N B] */
        uint8_t* pld = (uint8_t*)malloc(1 + ver_len);
        if (!pld) return;

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

    } else if (op == CMD_SUB_WRITE) {
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

    } else {
        uint8_t err = CMD_ERR_UNKNOWN_SUB;
        cmd_frame_t rsp = { .cmd = CMD_SYSTEM, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 1, .payload = &err };
        cmd_conn_send(conn, &rsp);
    }
}
