/**
 * main.c — 应用组装根（composition root）
 *
 * 只负责三件事：
 *   1. 初始化各模块（log / hw / cmd / app_sensor）
 *   2. 注册命令处理器与监听
 *   3. 进入命令服务器事件循环
 *
 * 业务逻辑（采集线程、命令处理）均在各 app_*.c 中，
 * 扩展新功能无需修改本文件，只需：
 *   - 新增 app_<feature>.c/.h
 *   - 在 main 中 create/register（各一行）
 */

#include "log/log.h"
#include "app_signal.h"
#include "app_cmd.h"
#include "app_led.h"
#include "app_sensor.h"
#include "app_system.h"
#include "app_camera.h"
#include "hw/dev/dev_sht30.h"
#include "hw/dev/dev_led.h"
#include "cmd/cmd_transport.h"
#include "cmd/cmd_subscription.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* ── 硬件路径 ───────────────────────────────────────────────────────── */

#define SHT30_SYSFS_PATH  "/sys/bus/i2c/devices/2-0044"
#define LED_NAME          "green_led"

/* ── 全局资源句柄 ───────────────────────────────────────────────────── */

static sht30_t*        g_sht30  = NULL;
static led_t*          g_led    = NULL;
static app_cmd_t*      g_cmd    = NULL;
static app_sensor_t*   g_sensor = NULL;
static app_camera_t*   g_camera = NULL;

/* ── 日志推送回调 ─────────────────────────────────────────────────── */

/**
 * log 模块回调：将每条日志通过订阅管理器推送给上位机。
 *
 * 构造 CMD_DATA_LOG 推送帧数据并调用 cmd_subscription_push。
 *
 * @param level  日志等级
 * @param msg    格式化日志消息
 * @param ctx    订阅管理器指针
 * @note         在 log_write_impl 持有锁时调用，不得调用 LOG_* 宏
 */
static void on_log_push(uint8_t level, const char* msg, void* ctx)
{
    cmd_subscription_mgr_t* sub_mgr = (cmd_subscription_mgr_t*)ctx;
    if (!sub_mgr || !msg) return;

    /* 构造推送数据: [level 1B, reserved 1B, timestamp 4B LE, msg N B] */
    uint32_t ts = (uint32_t)time(NULL);
    size_t msg_len = strlen(msg);
    size_t data_len = 1 + 1 + 4 + msg_len;
    uint8_t stack_buf[1024];
    uint8_t* buf = stack_buf;
    uint8_t* heap_buf = NULL;

    if (data_len > sizeof(stack_buf)) {
        heap_buf = (uint8_t*)malloc(data_len);
        if (!heap_buf) return;
        buf = heap_buf;
    }

    buf[0] = level;
    buf[1] = 0;  /* reserved */
    memcpy(buf + 2, &ts, 4);  /* LE timestamp */
    memcpy(buf + 6, msg, msg_len);

    cmd_subscription_push(sub_mgr, CMD_SYSTEM, CMD_DATA_LOG, buf, data_len);

    if (heap_buf) free(heap_buf);
}

/* ── 清理回调 ───────────────────────────────────────────────────────── */

/**
 * 进程退出时的统一清理（atexit 触发）。
 *
 * 关键顺序：
 *   1. 停止并销毁传感器模块 ← 其采集线程持有 sub_mgr，必须先退出
 *   2. 注销日志回调         ← 阻止 on_log_push 访问已释放的 sub_mgr
 *   3. 销毁 cmd 模块        ← 释放 sub_mgr/server/dispatcher
 *   4. 关闭硬件             ← led_close/sht30_close 可能调用 LOG_*
 *   5. 关闭日志模块         ← log_deinit 内部调用 LOG_INFO
 */
static void cleanup(void)
{
    LOG_INFO("Shutting down...");

    if (g_sensor) {
        app_sensor_stop(g_sensor);
        app_sensor_destroy(g_sensor);
        g_sensor = NULL;
    }

    if (g_camera) {
        app_camera_stop(g_camera);
        app_camera_destroy(g_camera);
        g_camera = NULL;
    }

    log_set_subscribe_callback(NULL, NULL);

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
    if (log_init("/tmp/project.log", LOG_DEBUG) != HW_OK) return 1;

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
    g_cmd = app_cmd_create();
    if (!g_cmd) {
        LOG_ERROR("Failed to create command module");
        return 1;
    }

    /* 注册命令处理器（扩展新功能只需增加一行） */
    app_cmd_register(g_cmd, CMD_LED,    cmd_handler_led,    g_led);
    g_sensor = app_sensor_create(g_sht30, app_cmd_get_sub_mgr(g_cmd));
    if (!g_sensor) {
        LOG_ERROR("Failed to create sensor module");
        return 1;
    }
    app_cmd_register(g_cmd, CMD_SENSOR, cmd_handler_sensor, g_sensor);
    app_cmd_register(g_cmd, CMD_SYSTEM, cmd_handler_system, app_cmd_get_sub_mgr(g_cmd));
    g_camera = app_camera_create();
    if (!g_camera) {
        LOG_ERROR("Failed to create camera module");
        return 1;
    }
    app_cmd_register(g_cmd, CMD_CAMERA, cmd_handler_camera, g_camera);

    /* 注册日志推送回调（必须在 app_cmd_create 之后，需要 sub_mgr） */
    log_set_subscribe_callback(on_log_push, app_cmd_get_sub_mgr(g_cmd));

    /* 添加监听 */
    app_cmd_add_listener_unix(g_cmd, CMD_DEFAULT_UNIX_SOCK_PATH);
    app_cmd_add_listener_tcp(g_cmd, 9527);

    /* 启动传感器采集线程 */
    if (app_sensor_start(g_sensor) != 0) {
        LOG_ERROR("Failed to start sensor thread");
        return 1;
    }

    /* 启动相机采集线程 */
    if (app_camera_start(g_camera) != 0) {
        LOG_ERROR("Failed to start camera thread");
        return 1;
    }

    /* 进入命令服务器事件循环（阻塞，退出后由 atexit cleanup 收尾） */
    app_cmd_run(g_cmd);

    LOG_INFO("Application exited normally");
    return 0;
}
