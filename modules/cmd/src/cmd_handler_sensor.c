/**
 * cmd_handler_sensor.c -- 传感器命令处理器
 *
 * 处理 CMD=0x02:
 *   SUB=0x02 读: PAYLOAD=空 → 返回当前温度和湿度(float BE)
 *   SUB=0x03 订阅: PAYLOAD=[data_id 2B BE, interval_ms 2B BE]
 *   SUB=0x04 取消订阅: PAYLOAD=[data_id 2B BE]
 *
 * 订阅/取消操作通过 handler context 的 sub_mgr 完成。
 */

#define _GNU_SOURCE
#include "cmd/cmd_dispatcher.h"
#include "cmd/cmd_server.h"
#include "cmd/cmd_protocol.h"
#include "cmd/cmd_subscription.h"
#include "hw/dev/dev_sht30.h"
#include "log/log.h"

#include <string.h>
#include <arpa/inet.h>  /* ntohs, htonl */

/**
 * 传感器 handler 专用上下文。
 */
typedef struct {
    sht30_t*                  sht30;
    cmd_subscription_mgr_t*   sub_mgr;
} sensor_handler_ctx_t;

/**
 * 传感器命令处理器（CMD=0x02）。
 *
 * ctx 应为 sensor_handler_ctx_t*，包含 sht30_t* 硬件句柄和 cmd_subscription_mgr_t*。
 * 子命令:
 *   READ       (0x02): 返回当前温湿度 [temp float BE, hum float BE]
 *   SUBSCRIBE  (0x03): PAYLOAD=[data_id 2B BE, interval_ms 2B BE]
 *   UNSUBSCRIBE(0x04): PAYLOAD=[data_id 2B BE]
 *
 * @param req   请求帧
 * @param conn  来源连接
 * @param ctx   sensor_handler_ctx_t* 句柄
 */
void cmd_handler_sensor(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx)
{
    sensor_handler_ctx_t* sctx = (sensor_handler_ctx_t*)ctx;
    sht30_t* sht30 = sctx ? sctx->sht30 : NULL;
    cmd_subscription_mgr_t* sub_mgr = sctx ? sctx->sub_mgr : NULL;

    uint8_t op = cmd_frame_sub_req(req->sub);

    if (op == CMD_SUB_READ) {
        /* 读一次传感器 */
        if (!sht30) {
            uint8_t err = CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_SENSOR, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        float temp_c = 0.0f;
        float humidity = 0.0f;
        hw_err_t ret_t = sht30_read_temperature(sht30, &temp_c);
        hw_err_t ret_h = sht30_read_humidity(sht30, &humidity);

        if (ret_t != HW_OK && ret_h != HW_OK) {
            uint8_t err = CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_SENSOR, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        /* PAYLOAD = [err 1B, temp float BE 4B, humidity float BE 4B] */
        uint8_t pld[9];
        pld[0] = CMD_ERR_OK;

        uint32_t tmp;
        memcpy(&tmp, &temp_c, 4);
        tmp = htonl(tmp);
        memcpy(pld + 1, &tmp, 4);

        memcpy(&tmp, &humidity, 4);
        tmp = htonl(tmp);
        memcpy(pld + 5, &tmp, 4);

        cmd_frame_t rsp = { .cmd = CMD_SENSOR, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 9, .payload = pld };
        cmd_conn_send(conn, &rsp);

        LOG_DEBUG("Sensor read: temp=%.1f°C humidity=%.1f%%", temp_c, humidity);

    } else if (op == CMD_SUB_SUBSCRIBE) {
        /* 订阅数据流: PAYLOAD=[data_id 2B BE, interval_ms 2B BE] */
        if (req->len < 4 || !sub_mgr) {
            uint8_t err = req->len < 4 ? CMD_ERR_PARAM : CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_SENSOR, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        uint16_t data_id_be, interval_be;
        memcpy(&data_id_be, req->payload, 2);
        memcpy(&interval_be, req->payload + 2, 2);
        uint16_t data_id  = ntohs(data_id_be);
        uint32_t interval = ntohs(interval_be);

        int rc = cmd_subscription_add(sub_mgr, data_id, interval, conn);

        uint8_t rsp_pld[5];
        rsp_pld[0] = (rc == 0) ? CMD_ERR_OK : CMD_ERR_PARAM;
        memcpy(rsp_pld + 1, &data_id_be, 2);
        memcpy(rsp_pld + 3, &interval_be, 2);

        cmd_frame_t rsp = { .cmd = CMD_SENSOR, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 5, .payload = rsp_pld };
        cmd_conn_send(conn, &rsp);

        LOG_DEBUG("Sensor subscribe: data_id=0x%04X interval=%ums rc=%d",
                  data_id, interval, rc);

    } else if (op == CMD_SUB_UNSUBSCRIBE) {
        /* 取消订阅: PAYLOAD=[data_id 2B BE] */
        if (req->len < 2 || !sub_mgr) {
            uint8_t err = req->len < 2 ? CMD_ERR_PARAM : CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_SENSOR, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        uint16_t data_id_be;
        memcpy(&data_id_be, req->payload, 2);
        uint16_t data_id = ntohs(data_id_be);

        int rc = cmd_subscription_remove(sub_mgr, data_id, conn);

        uint8_t rsp_pld[3];
        rsp_pld[0] = (rc == 0) ? CMD_ERR_OK : CMD_ERR_PARAM;
        memcpy(rsp_pld + 1, &data_id_be, 2);

        cmd_frame_t rsp = { .cmd = CMD_SENSOR, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 3, .payload = rsp_pld };
        cmd_conn_send(conn, &rsp);

        LOG_DEBUG("Sensor unsubscribe: data_id=0x%04X rc=%d", data_id, rc);

    } else {
        uint8_t err = CMD_ERR_UNKNOWN_SUB;
        cmd_frame_t rsp = { .cmd = CMD_SENSOR, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 1, .payload = &err };
        cmd_conn_send(conn, &rsp);
    }
}
