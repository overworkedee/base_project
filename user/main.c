#include "log/log.h"
#include "app_signal.h"
#include "app_cmd.h"
#include "hw/dev/dev_sht30.h"
#include "hw/dev/dev_led.h"
#include "cmd/cmd_subscription.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>  /* htonl */

/* ── 硬件路径 ───────────────────────────────────────────────────────── */

#define SHT30_SYSFS_PATH  "/sys/bus/i2c/devices/2-0044"
#define LED_NAME          "blue_led"

/* ── 全局资源句柄 ───────────────────────────────────────────────────── */

static sht30_t*               g_sht30 = NULL;
static led_t*                 g_led   = NULL;
static app_cmd_t*             g_cmd   = NULL;
static volatile int           g_running = 1;

/* ── 传感器采集线程 ─────────────────────────────────────────────────── */

/**
 * 传感器工作线程：按固定间隔采集温湿度，推送给所有订阅者。
 *
 * 退出条件：g_running == 0
 */
static void* sensor_thread(void* arg)
{
    (void)arg;

    LOG_INFO("Sensor thread started");

    cmd_subscription_mgr_t* sub_mgr = app_cmd_get_sub_mgr(g_cmd);

    while (g_running) {
        if (!g_sht30 || !sub_mgr) {
            sleep(1);
            continue;
        }

        float temp_c = 0.0f, humidity = 0.0f;

        if (sht30_read_temperature(g_sht30, &temp_c) == HW_OK) {
            uint32_t tmp;
            memcpy(&tmp, &temp_c, 4);
            tmp = htonl(tmp);
            uint8_t val[4];
            memcpy(val, &tmp, 4);

            int n = cmd_subscription_push(sub_mgr, CMD_SENSOR, CMD_DATA_TEMPERATURE, val, 4);
            if (n > 0) {
                LOG_DEBUG("Pushed temperature %.1f°C to %d subscriber(s)", temp_c, n);
            }
        }

        if (sht30_read_humidity(g_sht30, &humidity) == HW_OK) {
            uint32_t tmp;
            memcpy(&tmp, &humidity, 4);
            tmp = htonl(tmp);
            uint8_t val[4];
            memcpy(val, &tmp, 4);

            cmd_subscription_push(sub_mgr, CMD_SENSOR, CMD_DATA_HUMIDITY, val, 4);
        }

        sleep(1);
    }

    LOG_INFO("Sensor thread stopped");
    return NULL;
}

/* ── 清理回调 ───────────────────────────────────────────────────────── */

static void cleanup(void)
{
    LOG_INFO("Shutting down...");

    g_running = 0;  /* 通知传感器线程退出 */

    if (g_cmd) {
        app_cmd_destroy(g_cmd);
        g_cmd = NULL;
    }

    if (g_led) {
        led_close(g_led);
        g_led = NULL;
    }

    if (g_sht30) {
        sht30_close(g_sht30);
        g_sht30 = NULL;
    }

    log_deinit();
}

/* ── 应用程序入口 ───────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    atexit(cleanup);
    app_signal_init();

    /* 初始化日志 */
    hw_err_t ret = log_init("/tmp/project.log", LOG_DEBUG);
    if (ret != HW_OK) return 1;

    LOG_INFO("Application started");
    LOG_INFO("Platform: RK3588 Orange Pi 5 Plus");

    /* 初始化硬件 */
    g_sht30 = sht30_open(SHT30_SYSFS_PATH);
    if (!g_sht30) {
        LOG_ERROR("Failed to open SHT30 sensor");
        return 1;
    }
    LOG_INFO("SHT30 sensor initialized at %s", SHT30_SYSFS_PATH);

    g_led = led_open(LED_NAME);
    if (!g_led) {
        LOG_WARN("Failed to open LED '%s', LED commands unavailable", LED_NAME);
    } else {
        LOG_INFO("LED '%s' initialized", LED_NAME);
    }

    /* 初始化命令模块 */
    g_cmd = app_cmd_create(g_led, g_sht30);
    if (!g_cmd) {
        LOG_ERROR("Failed to create command module");
        return 1;
    }

    app_cmd_add_listener_unix(g_cmd, "/tmp/cmd.sock");
    app_cmd_add_listener_tcp(g_cmd, 9527);

    /* 启动传感器采集线程 */
    pthread_t sensor_tid;
    pthread_create(&sensor_tid, NULL, sensor_thread, NULL);

    /* 进入命令服务器事件循环（阻塞） */
    app_cmd_run(g_cmd);

    /* 等待传感器线程退出 */
    g_running = 0;
    pthread_join(sensor_tid, NULL);

    LOG_INFO("Application exited normally");
    return 0;
}
