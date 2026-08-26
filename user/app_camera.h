#ifndef APP_CAMERA_H
#define APP_CAMERA_H

#include "app_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 生命周期 ─────────────────────────────────────────────────────── */

typedef struct app_camera app_camera_t;

app_camera_t* app_camera_create(const app_cmd_svc_t* svc);
void app_camera_destroy(app_camera_t* camera);

/* ── 采集线程 ─────────────────────────────────────────────────────── */

int  app_camera_start(app_camera_t* camera);
void app_camera_stop(app_camera_t* camera);

#ifdef __cplusplus
}
#endif

#endif /* APP_CAMERA_H */