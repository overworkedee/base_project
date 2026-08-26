#ifndef APP_SYSTEM_H
#define APP_SYSTEM_H

#include "app_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 生命周期 ─────────────────────────────────────────────────────── */

typedef struct app_system app_system_t;

app_system_t* app_system_create(const app_cmd_svc_t* svc);
void app_system_destroy(app_system_t* system);

#ifdef __cplusplus
}
#endif

#endif /* APP_SYSTEM_H */