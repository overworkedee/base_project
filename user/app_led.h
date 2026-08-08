#ifndef APP_LED_H
#define APP_LED_H

#include "cmd/cmd_dispatcher.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 命令处理器（供 app_cmd_register 注册） ──────────────────────── */

void cmd_handler_led(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx);

#ifdef __cplusplus
}
#endif

#endif /* APP_LED_H */
