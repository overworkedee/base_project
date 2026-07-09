#include "log/log.h"
#include "app_signal.h"
#include "hw/dev/dev_sht30.h"
#include "hw/dev/dev_led.h"
#include "cmd/cmd_server.h"
#include "cmd/cmd_dispatcher.h"
#include "cmd/cmd_subscription.h"
#include "cmd/cmd_transport.h"
#include "cmd/cmd_protocol.h"
#include "cmd/cmd_handler_ctx.h"
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
static cmd_server_t*          g_server = NULL;
static cmd_dispatcher_t*      g_dispatcher = NULL;
static cmd_subscription_mgr_t* g_sub_mgr = NULL;
static cmd_handler_ctx_t      g_hctx;  /* handler 上下文 */
static volatile int           g_running = 1;

/* ── 前向声明 handler ───────────────────────────────────────────────── */

extern void cmd_handler_led(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx);
extern void cmd_handler_sensor(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx);
extern void cmd_handler_system(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx);

/* ── 服务器请求回调 ─────────────────────────────────────────────────── */

/**
 * 服务器收到完整帧时的回调。
 * 将请求分发到 dispatcher。
 */
static void on_request(const cmd_frame_t* req, cmd_conn_t* conn)
{
    cmd_dispatcher_dispatch(g_dispatcher, req, conn);
}

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

    while (g_running) {
        if (!g_sht30 || !g_sub_mgr) {
            sleep(1);
            continue;
        }

        float temp_c = 0.0f, humidity = 0.0f;

        if (sht30_read_temperature(g_sht30, &temp_c) == HW_OK) {
            /* 转为大端序 float */
            uint32_t tmp;
            memcpy(&tmp, &temp_c, 4);
            tmp = htonl(tmp);
            uint8_t val[4];
            memcpy(val, &tmp, 4);

            int n = cmd_subscription_push(g_sub_mgr, CMD_SENSOR, CMD_DATA_TEMPERATURE, val, 4);
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

            cmd_subscription_push(g_sub_mgr, CMD_SENSOR, CMD_DATA_HUMIDITY, val, 4);
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

    if (g_server) {
        cmd_server_stop(g_server);
        cmd_server_destroy(g_server);
        g_server = NULL;
    }

    if (g_dispatcher) {
        cmd_dispatcher_destroy(g_dispatcher);
        g_dispatcher = NULL;
    }

    if (g_sub_mgr) {
        cmd_subscription_destroy(g_sub_mgr);
        g_sub_mgr = NULL;
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

/**
 * 应用程序入口，初始化各模块后进入 epoll 事件循环。
 *
 * 命令行模块接管主循环，传感器采集在独立线程中运行。
 * Ctrl+C → exit(0) → atexit 自动调用 cleanup()。
 *
 * @param argc  命令行参数个数
 * @param argv  命令行参数数组
 * @return      0 正常退出，非零异常退出
 */
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

    /* 初始化命令模块基础设施 */
    g_sub_mgr = cmd_subscription_create();
    if (!g_sub_mgr) {
        LOG_ERROR("Failed to create subscription manager");
        return 1;
    }

    memset(&g_hctx, 0, sizeof(g_hctx));
    g_hctx.led     = g_led;
    g_hctx.sht30   = g_sht30;
    g_hctx.sub_mgr = g_sub_mgr;

    g_dispatcher = cmd_dispatcher_create(g_sub_mgr, &g_hctx);
    if (!g_dispatcher) {
        LOG_ERROR("Failed to create dispatcher");
        return 1;
    }

    cmd_dispatcher_register(g_dispatcher, CMD_LED,    cmd_handler_led);
    cmd_dispatcher_register(g_dispatcher, CMD_SENSOR, cmd_handler_sensor);
    cmd_dispatcher_register(g_dispatcher, CMD_SYSTEM, cmd_handler_system);

    /* 创建服务器 */
    g_server = cmd_server_create();
    if (!g_server) {
        LOG_ERROR("Failed to create cmd server");
        return 1;
    }
    cmd_server_set_handler(g_server, on_request);

    /* 注册监听端口 */
    int unix_fd = cmd_transport_listen_unix("/tmp/cmd.sock");
    if (unix_fd >= 0) {
        cmd_server_add_listener(g_server, unix_fd);
    }

    int tcp_fd = cmd_transport_listen_tcp(9527);
    if (tcp_fd >= 0) {
        cmd_server_add_listener(g_server, tcp_fd);
    }

    if (unix_fd < 0 && tcp_fd < 0) {
        LOG_ERROR("No listeners available, exiting");
        return 1;
    }

    /* 启动传感器采集线程 */
    pthread_t sensor_tid;
    pthread_create(&sensor_tid, NULL, sensor_thread, NULL);

    /* 进入事件循环（阻塞） */
    LOG_INFO("Entering command server event loop...");
    cmd_server_run(g_server);

    /* 等待传感器线程退出 */
    pthread_join(sensor_tid, NULL);

    LOG_INFO("Application exited normally");
    return 0;
}
