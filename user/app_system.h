#ifndef APP_SYSTEM_H
#define APP_SYSTEM_H

#include "cmd/cmd_dispatcher.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 命令处理器（供 app_cmd_register 注册） ──────────────────────── */

void cmd_handler_system(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx);

#ifdef __cplusplus
}
#endif

#endif /* APP_SYSTEM_H */
