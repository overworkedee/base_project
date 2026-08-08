#ifndef APP_CAMERA_H
#define APP_CAMERA_H

#include "cmd/cmd_dispatcher.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 生命周期 ─────────────────────────────────────────────────────── */

typedef struct app_camera app_camera_t;

app_camera_t* app_camera_create(void);
void app_camera_destroy(app_camera_t* camera);

/* ── 采集线程 ─────────────────────────────────────────────────────── */

int  app_camera_start(app_camera_t* camera);
void app_camera_stop(app_camera_t* camera);

/* ── 命令处理器（供 app_cmd_register 注册） ──────────────────────── */

void cmd_handler_camera(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx);

#ifdef __cplusplus
}
#endif

#endif /* APP_CAMERA_H */
