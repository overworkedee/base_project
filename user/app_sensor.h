#ifndef APP_SENSOR_H
#define APP_SENSOR_H

#include "app_registry.h"
#include "hw/dev/dev_sht30.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 生命周期 ─────────────────────────────────────────────────────── */

typedef struct app_sensor app_sensor_t;

app_sensor_t* app_sensor_create(sht30_t* sht30, const app_cmd_svc_t* svc);
void app_sensor_destroy(app_sensor_t* sensor);

/* ── 采集线程 ─────────────────────────────────────────────────────── */

int  app_sensor_start(app_sensor_t* sensor);
void app_sensor_stop(app_sensor_t* sensor);

#ifdef __cplusplus
}
#endif

#endif /* APP_SENSOR_H */