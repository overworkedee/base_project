#ifndef APP_LED_H
#define APP_LED_H

#include "app_registry.h"
#include "hw/dev/dev_led.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 生命周期 ─────────────────────────────────────────────────────── */

typedef struct app_led app_led_t;

app_led_t* app_led_create(led_t* led, const app_cmd_svc_t* svc);
void app_led_destroy(app_led_t* app);

#ifdef __cplusplus
}
#endif

#endif /* APP_LED_H */