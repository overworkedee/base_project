/**
 * main.c — 应用组装根（composition root）
 *
 * 只负责三件事：
 *   1. 初始化各模块（log / hw / app_cmd / 各 app_*）
 *   2. 通过 app_cmd_svc 能力表将命令服务注入各 app 模块
 *      （各 app 模块内部自行注册 handler，main 不感知具体 handler）
 *   3. 进入命令服务器事件循环
 *
 * 业务逻辑（采集线程、命令处理）均在各 app_*.c 中，
 * 扩展新功能只需：
 *   - 新增 app_<feature>.c/.h
 *   - main 中 create（一行）+ cleanup 中 destroy（一行）
 */

#include "log/log.h"
#include "app_signal.h"
#include "app_cmd.h"
#include "app_led.h"
#include "app_sensor.h"
#include "app_system.h"
#include "app_camera.h"
#include "app_registry.h"
#include "hw/dev/dev_sht30.h"
#include "hw/dev/dev_led.h"
#include "cmd/cmd_transport.h"
#include "cmd/cmd_frame.h"
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
static app_led_t*      g_appled = NULL;
static app_system_t*   g_system = NULL;

/* ── 日志推送回调 ─────────────────────────────────────────────────── */

/**
 * log 模块回调：将每条日志通过能力表推送给上位机订阅者。
 *
 * 通过 app_cmd_svc_t.publish_data 完成推送，
 * 不直接依赖 cmd 内部订阅管理器类型。
 *
 * @param level  日志等级
 * @param msg    格式化日志消息
 * @param ctx    回调上下文（app_cmd_svc_t*）
 * @note         在 log_write_impl 持有锁时调用，不得调用 LOG_* 宏
 */
static void on_log_push(uint8_t level, const char* msg, void* ctx)
{
    const app_cmd_svc_t* svc = (const app_cmd_svc_t*)ctx;
    if (!svc || !svc->publish_data || !msg) return;

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

    svc->publish_data(svc->owner, CMD_SYSTEM, CMD_DATA_LOG, buf, data_len);

    if (heap_buf) free(heap_buf);
}

/* ── 清理回调 ───────────────────────────────────────────────────────── */

/**
 * 进程退出时的统一清理（atexit 触发）。
 *
 * 关键顺序：
 *   1. 停止并销毁各 app 模块 ← 采集线程持有 svc（指向 g_cmd 内部），必须先退出
 *   2. 注销日志回调         ← 阻止 on_log_push 访问已释放的 svc
 *   3. 销毁 cmd 模块        ← 释放 server/dispatcher/sub_mgr
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

    if (g_appled) {
        app_led_destroy(g_appled);
        g_appled = NULL;
    }

    if (g_system) {
        app_system_destroy(g_system);
        g_system = NULL;
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
        /* 传感器缺失不影响主流程：sensor 模块会跳过采集，命令返回 HW_ERR */
        LOG_WARN("Failed to open SHT30 sensor, sensor commands unavailable");
    } else {
        LOG_INFO("SHT30 sensor initialized at %s", SHT30_SYSFS_PATH);
    }

    g_led = led_open(LED_NAME);
    if (!g_led) {
        LOG_WARN("Failed to open LED '%s', LED commands unavailable", LED_NAME);
    } else {
        LOG_INFO("LED '%s' initialized", LED_NAME);
    }

    /* 初始化命令模块（能力表 owner） */
    g_cmd = app_cmd_create();
    if (!g_cmd) {
        LOG_ERROR("Failed to create command module");
        return 1;
    }

    const app_cmd_svc_t* svc = app_cmd_get_svc(g_cmd);

    /* 创建各 app 模块（内部自动注册各自的命令 handler） */
    g_appled = app_led_create(g_led, svc);
    g_sensor = app_sensor_create(g_sht30, svc);
    g_system = app_system_create(svc);
    g_camera = app_camera_create(svc);
    if (!g_appled || !g_sensor || !g_system || !g_camera) {
        LOG_ERROR("Failed to create app modules");
        return 1;
    }

    /* 注册日志推送回调（通过能力表推送，svc 随 g_cmd 生命周期有效） */
    log_set_subscribe_callback(on_log_push, (void*)svc);

    /* 添加监听 */
    app_cmd_add_listener_unix(g_cmd, CMD_DEFAULT_UNIX_SOCK_PATH);
    app_cmd_add_listener_tcp(g_cmd, 9527);

    /* 启动传感器采集线程 */
    if (app_sensor_start(g_sensor) != 0) {
        LOG_ERROR("Failed to start sensor thread");
        return 1;
    }

    /* 启动相机监控线程（自动拉起 RTSP 推流） */
    if (app_camera_start(g_camera) != 0) {
        LOG_ERROR("Failed to start camera thread");
        return 1;
    }

    /* 进入命令服务器事件循环（阻塞，退出后由 atexit cleanup 收尾） */
    app_cmd_run(g_cmd);

    LOG_INFO("Application exited normally");
    return 0;
}