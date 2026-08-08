#ifndef APP_SENSOR_H
#define APP_SENSOR_H

#include "hw/dev/dev_sht30.h"
#include "cmd/cmd_dispatcher.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 生命周期 ─────────────────────────────────────────────────────── */

typedef struct app_sensor app_sensor_t;

app_sensor_t* app_sensor_create(sht30_t* sht30, cmd_subscription_mgr_t* sub_mgr);
void app_sensor_destroy(app_sensor_t* sensor);

/* ── 采集线程 ─────────────────────────────────────────────────────── */

int  app_sensor_start(app_sensor_t* sensor);
void app_sensor_stop(app_sensor_t* sensor);

/* ── 命令处理器（供 app_cmd_register 注册） ──────────────────────── */

void cmd_handler_sensor(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx);

#ifdef __cplusplus
}
#endif

#endif /* APP_SENSOR_H */
