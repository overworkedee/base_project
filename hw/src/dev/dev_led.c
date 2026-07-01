/**
 * @file dev_led.c
 * @brief LED sysfs 控制实现
 *
 * 通过 /sys/class/leds/<name>/ 接口控制 GPIO LED。
 * 内部使用 hw_mutex_t 保证所有 sysfs 操作的线程安全。
 */

#include "hw/dev/dev_led.h"
#include "hw/hw_mutex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* ── 内部结构 ─────────────────────────────────────────────────────── */

struct led_ctx {
    char        name[64];         /* LED 名称（label）                     */
    int         max_brightness;   /* 最大亮度值，从 max_brightness 读取     */
    hw_mutex_t  lock;             /* 线程安全锁                            */
};

/* ── 内部辅助函数 ─────────────────────────────────────────────────── */

/**
 * 向 sysfs 文件写入字符串。
 *
 * @param path   sysfs 文件路径
 * @param value  要写入的字符串
 * @return       HW_OK 成功，HW_ERR_IO 失败
 */
static hw_err_t led_write_sysfs(const char* path, const char* value)
{
    if (!path || !value) return HW_ERR_PARAM;

    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "[hw:led] fopen(%s) write failed: %s\n", path, strerror(errno));
        return HW_ERR_IO;
    }
    int ret = fputs(value, f);
    if (ret < 0) {
        fprintf(stderr, "[hw:led] fputs(%s) failed: %s\n", path, strerror(errno));
        fclose(f);
        return HW_ERR_IO;
    }
    fclose(f);
    return HW_OK;
}

/**
 * 从 sysfs 文件读取一行字符串（自动去除尾部换行符）。
 *
 * @param path  sysfs 文件路径
 * @param buf   输出缓冲区
 * @param len   缓冲区大小
 * @return      HW_OK 成功，HW_ERR_IO 失败，HW_ERR_PARAM 参数非法
 */
static hw_err_t led_read_sysfs(const char* path, char* buf, size_t len)
{
    if (!path || !buf || len == 0) return HW_ERR_PARAM;

    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "[hw:led] fopen(%s) read failed: %s\n", path, strerror(errno));
        return HW_ERR_IO;
    }
    if (!fgets(buf, (int)len, f)) {
        fprintf(stderr, "[hw:led] fgets(%s) failed: %s\n", path, strerror(errno));
        fclose(f);
        return HW_ERR_IO;
    }
    fclose(f);

    /* 去除尾部换行符 */
    size_t slen = strlen(buf);
    if (slen > 0 && buf[slen - 1] == '\n') {
        buf[slen - 1] = '\0';
    }
    return HW_OK;
}

/* ── 公开 API ─────────────────────────────────────────────────────── */

/**
 * 打开 LED 设备。
 *
 * 检查 /sys/class/leds/<name>/brightness 是否可写以验证 LED 存在，
 * 读取 max_brightness 作为后续 led_on 的目标值。
 *
 * @param name  LED 的 label 名称
 * @return      成功返回 LED 句柄，失败返回 NULL
 */
led_t* led_open(const char* name)
{
    if (!name) return NULL;

    /* 校验 sysfs 路径存在且可写 */
    char path[256];
    snprintf(path, sizeof(path), "/sys/class/leds/%s/brightness", name);
    if (access(path, W_OK) != 0) {
        fprintf(stderr, "[hw:led] LED '%s' not accessible at %s: %s\n",
                name, path, strerror(errno));
        return NULL;
    }

    /* 分配句柄 */
    led_t* led = (led_t*)calloc(1, sizeof(led_t));
    if (!led) {
        fprintf(stderr, "[hw:led] calloc failed for LED '%s'\n", name);
        return NULL;
    }
    strncpy(led->name, name, sizeof(led->name) - 1);
    led->name[sizeof(led->name) - 1] = '\0';

    /* 初始化互斥锁 */
    hw_err_t ret = hw_mutex_init(&led->lock);
    if (ret != HW_OK) {
        fprintf(stderr, "[hw:led] mutex init failed: %s\n", hw_err_str(ret));
        free(led);
        return NULL;
    }

    /* 读取 max_brightness */
    snprintf(path, sizeof(path), "/sys/class/leds/%s/max_brightness", name);
    char buf[32] = {0};
    ret = led_read_sysfs(path, buf, sizeof(buf));
    if (ret != HW_OK) {
        hw_mutex_destroy(&led->lock);
        free(led);
        return NULL;
    }
    led->max_brightness = atoi(buf);
    if (led->max_brightness <= 0) {
        led->max_brightness = 1;  /* 回退：至少为 1 */
    }

    return led;
}

/**
 * 关闭 LED 设备并释放所有资源。
 *
 * @param led  LED 句柄，可为 NULL
 */
void led_close(led_t* led)
{
    if (!led) return;
    hw_mutex_destroy(&led->lock);
    free(led);
}

/**
 * 打开 LED（写入 max_brightness）。
 *
 * @param led  LED 句柄
 * @return     HW_OK 成功，HW_ERR_PARAM 或 HW_ERR_IO 失败
 * @note       线程安全
 */
hw_err_t led_on(led_t* led)
{
    if (!led) return HW_ERR_PARAM;

    hw_err_t ret = hw_mutex_lock(&led->lock);
    if (ret != HW_OK) return ret;

    char path[256];
    char val[32];
    snprintf(path, sizeof(path), "/sys/class/leds/%s/brightness", led->name);
    snprintf(val, sizeof(val), "%d", led->max_brightness);
    ret = led_write_sysfs(path, val);

    hw_mutex_unlock(&led->lock);
    return ret;
}

/**
 * 关闭 LED（写入 0）。
 *
 * @param led  LED 句柄
 * @return     HW_OK 成功，HW_ERR_PARAM 或 HW_ERR_IO 失败
 * @note       线程安全
 */
hw_err_t led_off(led_t* led)
{
    if (!led) return HW_ERR_PARAM;

    hw_err_t ret = hw_mutex_lock(&led->lock);
    if (ret != HW_OK) return ret;

    char path[256];
    snprintf(path, sizeof(path), "/sys/class/leds/%s/brightness", led->name);
    ret = led_write_sysfs(path, "0");

    hw_mutex_unlock(&led->lock);
    return ret;
}

/**
 * 设置 LED trigger 模式。
 *
 * @param led      LED 句柄
 * @param trigger  目标 trigger 模式
 * @return         HW_OK 成功，HW_ERR_PARAM 或 HW_ERR_IO 失败
 * @note           线程安全
 */
hw_err_t led_set_trigger(led_t* led, led_trigger_t trigger)
{
    if (!led) return HW_ERR_PARAM;

    const char* trigger_str = NULL;
    switch (trigger) {
    case LED_TRIGGER_HEARTBEAT: trigger_str = "heartbeat"; break;
    case LED_TRIGGER_NONE:      trigger_str = "none";      break;
    default:                    return HW_ERR_PARAM;
    }

    hw_err_t ret = hw_mutex_lock(&led->lock);
    if (ret != HW_OK) return ret;

    char path[256];
    snprintf(path, sizeof(path), "/sys/class/leds/%s/trigger", led->name);
    ret = led_write_sysfs(path, trigger_str);

    hw_mutex_unlock(&led->lock);
    return ret;
}

/**
 * 读取 LED 当前亮度值。
 *
 * @param led         LED 句柄
 * @param brightness  输出参数，接收当前亮度值
 * @return            HW_OK 成功，HW_ERR_PARAM 或 HW_ERR_IO 失败
 * @note              线程安全
 */
hw_err_t led_get_brightness(led_t* led, int* brightness)
{
    if (!led || !brightness) return HW_ERR_PARAM;

    hw_err_t ret = hw_mutex_lock(&led->lock);
    if (ret != HW_OK) return ret;

    char path[256];
    char buf[32] = {0};
    snprintf(path, sizeof(path), "/sys/class/leds/%s/brightness", led->name);
    ret = led_read_sysfs(path, buf, sizeof(buf));
    if (ret == HW_OK) {
        *brightness = atoi(buf);
    }

    hw_mutex_unlock(&led->lock);
    return ret;
}

/**
 * 读取 LED 当前 trigger 模式。
 *
 * 解析 /sys/class/leds/<name>/trigger 文件中 [...] 包裹的当前激活项。
 *
 * @param led      LED 句柄
 * @param trigger  输出参数，接收当前 trigger 模式
 * @return         HW_OK 成功，HW_ERR_PARAM 或 HW_ERR_IO 失败
 * @note           线程安全。若当前 trigger 非 heartbeat/none，返回 LED_TRIGGER_NONE。
 */
hw_err_t led_get_trigger(led_t* led, led_trigger_t* trigger)
{
    if (!led || !trigger) return HW_ERR_PARAM;

    hw_err_t ret = hw_mutex_lock(&led->lock);
    if (ret != HW_OK) return ret;

    char path[256];
    char buf[1024] = {0};
    snprintf(path, sizeof(path), "/sys/class/leds/%s/trigger", led->name);
    ret = led_read_sysfs(path, buf, sizeof(buf));
    if (ret != HW_OK) {
        hw_mutex_unlock(&led->lock);
        return ret;
    }

    /* 解析 [...] 中当前激活的 trigger */
    char* start = strchr(buf, '[');
    char* end   = strchr(buf, ']');
    if (!start || !end || end <= start) {
        fprintf(stderr, "[hw:led] failed to parse trigger from %s\n", path);
        hw_mutex_unlock(&led->lock);
        return HW_ERR_IO;
    }
    *end = '\0';
    const char* active = start + 1;

    if (strcmp(active, "heartbeat") == 0) {
        *trigger = LED_TRIGGER_HEARTBEAT;
    } else {
        *trigger = LED_TRIGGER_NONE;
    }

    hw_mutex_unlock(&led->lock);
    return HW_OK;
}
