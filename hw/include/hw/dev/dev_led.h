#ifndef DEV_LED_H
#define DEV_LED_H

#include <stddef.h>
#include "hw/hw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 类型定义 ─────────────────────────────────────────────────────── */

/** LED trigger 模式 */
typedef enum {
    LED_TRIGGER_NONE      = 0,  /* 无闪烁，常亮/常灭由 brightness 控制 */
    LED_TRIGGER_HEARTBEAT = 1,  /* 心跳闪烁，内核 heartbeat trigger       */
} led_trigger_t;

/** 不透明 LED 句柄 */
typedef struct led_ctx led_t;

/* ── 生命周期 ─────────────────────────────────────────────────────── */

/**
 * 打开 LED 设备。
 *
 * 校验 /sys/class/leds/<name>/ 路径是否存在，读取 max_brightness 并初始化互斥锁。
 *
 * @param name  LED 的 label 名称，对应设备树中的 label 属性，如 "blue_led"
 * @return      成功返回 LED 句柄，失败（路径不存在、内存不足）返回 NULL
 */
led_t* led_open(const char* name);

/**
 * 关闭 LED 设备并释放所有资源。
 *
 * @param led  LED 句柄，可为 NULL（此时无操作）
 */
void led_close(led_t* led);

/* ── 开关控制 ─────────────────────────────────────────────────────── */

/**
 * 打开 LED（写入 max_brightness 到 brightness 文件）。
 *
 * @param led  LED 句柄
 * @return     HW_OK 成功，HW_ERR_PARAM（led 为 NULL），HW_ERR_IO（写入失败）
 * @note       线程安全
 */
hw_err_t led_on(led_t* led);

/**
 * 关闭 LED（写入 0 到 brightness 文件）。
 *
 * @param led  LED 句柄
 * @return     HW_OK 成功，HW_ERR_PARAM（led 为 NULL），HW_ERR_IO（写入失败）
 * @note       线程安全
 */
hw_err_t led_off(led_t* led);

/* ── Trigger 控制 ─────────────────────────────────────────────────── */

/**
 * 设置 LED 闪烁模式。
 *
 * 向 /sys/class/leds/<name>/trigger 写入 trigger 名称。
 *
 * @param led      LED 句柄
 * @param trigger  目标 trigger 模式（LED_TRIGGER_NONE 或 LED_TRIGGER_HEARTBEAT）
 * @return         HW_OK 成功，HW_ERR_PARAM（参数非法），HW_ERR_IO（写入失败）
 * @note           线程安全
 */
hw_err_t led_set_trigger(led_t* led, led_trigger_t trigger);

/* ── 状态读取 ─────────────────────────────────────────────────────── */

/**
 * 读取 LED 当前亮度值。
 *
 * 从 /sys/class/leds/<name>/brightness 读取当前值。
 *
 * @param led         LED 句柄
 * @param brightness  输出参数，接收当前亮度值（0 ~ max_brightness）
 * @return            HW_OK 成功，HW_ERR_PARAM（参数为 NULL），HW_ERR_IO（读取失败）
 * @note              线程安全
 */
hw_err_t led_get_brightness(led_t* led, int* brightness);

/**
 * 读取 LED 当前 trigger 模式。
 *
 * 从 /sys/class/leds/<name>/trigger 读取，解析 [...] 中当前激活的 trigger。
 * 若当前 trigger 非 heartbeat/none，则返回 LED_TRIGGER_NONE。
 *
 * @param led      LED 句柄
 * @param trigger  输出参数，接收当前 trigger 模式
 * @return         HW_OK 成功，HW_ERR_PARAM（参数为 NULL），HW_ERR_IO（读取/解析失败）
 * @note           线程安全
 */
hw_err_t led_get_trigger(led_t* led, led_trigger_t* trigger);

#ifdef __cplusplus
}
#endif

#endif /* DEV_LED_H */
