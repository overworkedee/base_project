#ifndef APP_CMD_H
#define APP_CMD_H

#include <stdint.h>
#include "cmd/cmd_dispatcher.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 前向声明 */
typedef struct app_cmd              app_cmd_t;
typedef struct cmd_subscription_mgr cmd_subscription_mgr_t;

/* ── 生命周期 ─────────────────────────────────────────────────────── */

app_cmd_t* app_cmd_create(void);
void       app_cmd_destroy(app_cmd_t* cmd);

/* ── 命令注册 ─────────────────────────────────────────────────────── */

int app_cmd_register(app_cmd_t* cmd, uint8_t cmd_cls,
                     cmd_handler_fn handler, void* ctx);

/* ── 监听 ─────────────────────────────────────────────────────────── */

int app_cmd_add_listener_unix(app_cmd_t* cmd, const char* path);
int app_cmd_add_listener_tcp(app_cmd_t* cmd, uint16_t port);

/* ── 事件循环 ─────────────────────────────────────────────────────── */

int  app_cmd_run(app_cmd_t* cmd);
void app_cmd_stop(app_cmd_t* cmd);

/* ── 内部对象访问 ─────────────────────────────────────────────────── */

cmd_subscription_mgr_t* app_cmd_get_sub_mgr(app_cmd_t* cmd);

#ifdef __cplusplus
}
#endif

#endif /* APP_CMD_H */
