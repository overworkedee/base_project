/**
 * app_sensor.c — 传感器应用模块
 *
 * 整合 SHT30 温湿度采集与 CMD_SENSOR 命令处理：
 *   - 后台采集线程周期性读取温湿度并推送给订阅者
 *   - cmd_handler_sensor 处理上位机的读/订阅/取消订阅请求
 *
 * 采集线程与命令处理器共享 app_sensor_t 上下文，
 * main.c 只需 create/start/stop/destroy 四个调用。
 */

#include "app_sensor.h"
#include "cmd/cmd_server.h"
#include "cmd/cmd_protocol.h"
#include "cmd/cmd_subscription.h"
#include "log/log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>  /* htonl, ntohs */

/* ── 内部结构 ───────────────────────────────────────────────────────── */

struct app_sensor {
    sht30_t*                sht30;    /* SHT30 硬件句柄 */
    cmd_subscription_mgr_t* sub_mgr;  /* 订阅管理器（数据推送） */
    pthread_t               thread;   /* 采集线程 */
    int                     started;  /* 线程是否已启动 */
    volatile int            running;  /* 线程运行标志 */
};

/* ── 采集线程 ───────────────────────────────────────────────────────── */

/**
 * 周期性读取温湿度并通过订阅管理器推送。
 *
 * 每秒读取一次 SHT30，将结果以大端序 float 推送；
 * 设备或订阅管理器缺失时休眠等待（容错启动顺序）。
 *
 * @param arg  app_sensor_t* 上下文
 * @return     NULL
 * @note       退出条件：app_sensor_stop 将 running 置 0
 */
static void* sensor_thread(void* arg)
{
    app_sensor_t* sensor = (app_sensor_t*)arg;

    LOG_INFO("Sensor thread started");

    while (sensor->running) {
        if (!sensor->sht30 || !sensor->sub_mgr) {
            sleep(1);
            continue;
        }

        float temp_c = 0.0f, humidity = 0.0f;

        if (sht30_read_temperature(sensor->sht30, &temp_c) == HW_OK) {
            uint32_t tmp;
            memcpy(&tmp, &temp_c, 4);
            tmp = htonl(tmp);
            uint8_t val[4];
            memcpy(val, &tmp, 4);

            int n = cmd_subscription_push(sensor->sub_mgr, CMD_SENSOR,
                                          CMD_DATA_TEMPERATURE, val, 4);
            if (n > 0) {
                LOG_DEBUG("Pushed temperature %.1f°C to %d subscriber(s)", temp_c, n);
            }
        }

        if (sht30_read_humidity(sensor->sht30, &humidity) == HW_OK) {
            uint32_t tmp;
            memcpy(&tmp, &humidity, 4);
            tmp = htonl(tmp);
            uint8_t val[4];
            memcpy(val, &tmp, 4);

            cmd_subscription_push(sensor->sub_mgr, CMD_SENSOR,
                                  CMD_DATA_HUMIDITY, val, 4);
        }

        sleep(1);
    }

    LOG_INFO("Sensor thread stopped");
    return NULL;
}

/* ── 生命周期 ───────────────────────────────────────────────────────── */

/**
 * 创建传感器应用模块。
 *
 * @param sht30    SHT30 硬件句柄（可为 NULL，采集线程会跳过）
 * @param sub_mgr  订阅管理器（数据推送目标），可为 NULL
 * @return         成功返回实例指针，失败返回 NULL
 */
app_sensor_t* app_sensor_create(sht30_t* sht30, cmd_subscription_mgr_t* sub_mgr)
{
    app_sensor_t* sensor = (app_sensor_t*)calloc(1, sizeof(app_sensor_t));
    if (!sensor) return NULL;

    sensor->sht30   = sht30;
    sensor->sub_mgr = sub_mgr;
    return sensor;
}

/**
 * 释放传感器应用模块。
 *
 * @param sensor  传感器应用模块（可为 NULL）
 * @note           调用前必须先 app_sensor_stop，否则线程仍在使用上下文
 */
void app_sensor_destroy(app_sensor_t* sensor)
{
    if (!sensor) return;
    free(sensor);
}

/* ── 采集线程 ───────────────────────────────────────────────────────── */

/**
 * 启动传感器采集线程。
 *
 * @param sensor  传感器应用模块
 * @return        0 成功，-1 失败（参数错误或线程创建失败）
 * @note          重复调用返回 -1（线程已启动）
 */
int app_sensor_start(app_sensor_t* sensor)
{
    if (!sensor || sensor->started) return -1;

    sensor->running = 1;
    if (pthread_create(&sensor->thread, NULL, sensor_thread, sensor) != 0) {
        sensor->running = 0;
        return -1;
    }

    sensor->started = 1;
    return 0;
}

/**
 * 停止采集线程并等待其退出。
 *
 * @param sensor  传感器应用模块
 * @note           未启动时无操作；调用后不可再 start（需重新 create）
 */
void app_sensor_stop(app_sensor_t* sensor)
{
    if (!sensor || !sensor->started) return;

    sensor->running = 0;
    pthread_join(sensor->thread, NULL);
    sensor->started = 0;
}

/* ── 命令处理器 ─────────────────────────────────────────────────────── */

/**
 * 传感器命令处理器（CMD=0x02）。
 *
 * ctx 应为 app_sensor_t*，内部取出 sht30 句柄和订阅管理器。
 *
 * 子命令:
 *   READ       (0x02): 返回当前温湿度 [temp float BE, hum float BE]
 *   SUBSCRIBE  (0x03): PAYLOAD=[data_id 2B BE, interval_ms 2B BE]
 *   UNSUBSCRIBE(0x04): PAYLOAD=[data_id 2B BE]
 *
 * @param req   请求帧
 * @param conn  来源连接
 * @param ctx   app_sensor_t* 句柄
 */
void cmd_handler_sensor(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx)
{
    app_sensor_t* sensor = (app_sensor_t*)ctx;
    sht30_t* sht30 = sensor ? sensor->sht30 : NULL;
    cmd_subscription_mgr_t* sub_mgr = sensor ? sensor->sub_mgr : NULL;

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
