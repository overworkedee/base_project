#ifndef APP_CMD_H
#define APP_CMD_H

#include <stdint.h>
#include "app_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 前向声明 */
typedef struct app_cmd app_cmd_t;

/* ── 生命周期 ─────────────────────────────────────────────────────── */

app_cmd_t* app_cmd_create(void);
void       app_cmd_destroy(app_cmd_t* cmd);

/* ── 服务能力表 ───────────────────────────────────────────────────── */

/**
 * 获取命令服务能力表（handler 注册 / 数据推送 / 订阅）。
 *
 * 各 app 模块通过此表注入回调，避免直接依赖 cmd 内部类型。
 *
 * @param cmd  命令模块实例
 * @return     能力表指针（随 app_cmd_t 生命周期有效），cmd 为 NULL 时返回 NULL
 */
const app_cmd_svc_t* app_cmd_get_svc(app_cmd_t* cmd);

/* ── 监听 ─────────────────────────────────────────────────────────── */

int app_cmd_add_listener_unix(app_cmd_t* cmd, const char* path);
int app_cmd_add_listener_tcp(app_cmd_t* cmd, uint16_t port);

/* ── 事件循环 ─────────────────────────────────────────────────────── */

int  app_cmd_run(app_cmd_t* cmd);
void app_cmd_stop(app_cmd_t* cmd);

#ifdef __cplusplus
}
#endif

#endif /* APP_CMD_H */