/**
 * cmd_handler_led.c -- LED 命令处理器
 *
 * 处理 CMD=0x01:
 *   SUB=0x01 写: PAYLOAD=[led_id 1B, state 1B]  → 调用 led_on/led_off
 *   SUB=0x02 读: PAYLOAD=[led_id 1B]            → 读取 brightness，返回 [led_id, state]
 *
 * 每个处理器通过 void* ctx 获取硬件句柄。
 * ctx 指向 cmd_handler_ctx_t 结构（定义在 cmd_handler_ctx.h）。
 */

#define _GNU_SOURCE
#include "cmd/cmd_dispatcher.h"
#include "cmd/cmd_server.h"
#include "cmd/cmd_protocol.h"
#include "hw/dev/dev_led.h"
#include "log/log.h"

#include <string.h>

/**
 * LED 命令处理器（CMD=0x01）。
 *
 * ctx 应为 led_t* 硬件句柄（可为 NULL，此时所有操作返回 CMD_ERR_HARDWARE）。
 *
 * 子命令:
 *   WRITE(0x01): PAYLOAD=[led_id 1B, state 1B]  0=关 1=开 → 调用 led_on/led_off
 *   READ (0x02): PAYLOAD=[led_id 1B]             → 返回 [err, led_id, state]
 *
 * @param req   请求帧
 * @param conn  来源连接
 * @param ctx   led_t* 句柄
 */
void cmd_handler_led(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx)
{
    led_t* led = (led_t*)ctx;
    if (!led) {
        /* 无 LED 硬件，返回错误 */
        uint8_t err = CMD_ERR_HARDWARE;
        cmd_frame_t rsp = { .cmd = CMD_LED, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 1, .payload = &err };
        cmd_conn_send(conn, &rsp);
        return;
    }

    uint8_t op = cmd_frame_sub_req(req->sub);

    if (op == CMD_SUB_WRITE) {
        /* 写 LED: PAYLOAD=[led_id, state] */
        if (req->len < 2) {
            uint8_t err = CMD_ERR_PARAM;
            cmd_frame_t rsp = { .cmd = CMD_LED, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        uint8_t led_id = req->payload[0];
        uint8_t state  = req->payload[1];
        hw_err_t ret;

        if (state) {
            ret = led_on(led);
        } else {
            ret = led_off(led);
        }

        uint8_t rsp_pld[3];
        rsp_pld[0] = (ret == HW_OK) ? CMD_ERR_OK : CMD_ERR_HARDWARE;
        rsp_pld[1] = led_id;
        rsp_pld[2] = state;

        cmd_frame_t rsp = { .cmd = CMD_LED, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 3, .payload = rsp_pld };
        cmd_conn_send(conn, &rsp);

        LOG_DEBUG("LED write: id=%d state=%d result=%d", led_id, state, ret);

    } else if (op == CMD_SUB_READ) {
        /* 读 LED: PAYLOAD=[led_id] */
        if (req->len < 1) {
            uint8_t err = CMD_ERR_PARAM;
            cmd_frame_t rsp = { .cmd = CMD_LED, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        uint8_t led_id = req->payload[0];
        int brightness = 0;
        hw_err_t ret = led_get_brightness(led, &brightness);

        uint8_t rsp_pld[3];
        rsp_pld[0] = (ret == HW_OK) ? CMD_ERR_OK : CMD_ERR_HARDWARE;
        rsp_pld[1] = led_id;
        rsp_pld[2] = (brightness > 0) ? 1 : 0;

        cmd_frame_t rsp = { .cmd = CMD_LED, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 3, .payload = rsp_pld };
        cmd_conn_send(conn, &rsp);

        LOG_DEBUG("LED read: id=%d brightness=%d", led_id, brightness);

    } else {
        /* 未知子命令 */
        uint8_t err = CMD_ERR_UNKNOWN_SUB;
        cmd_frame_t rsp = { .cmd = CMD_LED, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 1, .payload = &err };
        cmd_conn_send(conn, &rsp);
    }
}
