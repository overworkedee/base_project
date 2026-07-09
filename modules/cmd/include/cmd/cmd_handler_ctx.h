#ifndef CMD_HANDLER_CTX_H
#define CMD_HANDLER_CTX_H

#ifdef __cplusplus
extern "C" {
#endif

/* 前向声明 hw 类型 */
typedef struct led_ctx   led_t;
typedef struct sht30_ctx sht30_t;

/* 前向声明 cmd_subscription_mgr_t */
typedef struct cmd_subscription_mgr cmd_subscription_mgr_t;

/**
 * handler 共享上下文，通过 dispatcher 的 void* ctx 传递。
 *
 * 所有 handler 通过此结构访问硬件句柄和订阅管理器。
 * 未初始化的句柄设为 NULL，handler 内检查 NULL 决定是否可用。
 */
typedef struct {
    led_t*                    led;      /* LED 句柄，可为 NULL       */
    sht30_t*                  sht30;    /* SHT30 传感器句柄，可为 NULL */
    cmd_subscription_mgr_t*   sub_mgr;  /* 订阅管理器，可为 NULL     */
} cmd_handler_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* CMD_HANDLER_CTX_H */
