#include "log/log.h"
#include "app_signal.h"
#include "hw/dev/dev_sht30.h"
#include <stdlib.h>
#include <unistd.h>

/* ── 全局资源句柄（cleanup 回调需要访问） ────────────────────────── */

#define SHT30_SYSFS_PATH  "/sys/bus/i2c/devices/2-0044"

static sht30_t* g_sht30 = NULL;   /* SHT30 传感器句柄 */

/* ── 清理回调（由 atexit / 信号退出时自动调用） ──────────────────── */

/**
 * 资源清理回调。
 *
 * 通过 atexit 注册，在正常 return 或信号触发 exit() 时自动执行。
 * 在此处释放所有 open 的设备资源。
 */
static void cleanup(void)
{
    LOG_INFO("Shutting down...");

    sht30_close(g_sht30);

    log_deinit();
}

/* ── 应用程序入口 ─────────────────────────────────────────────────── */

/**
 * 应用程序入口，初始化日志后进入主循环。
 *
 * 通过 app_signal + atexit 实现信号驱动的优雅退出：
 * Ctrl+C → exit(0) → 自动调用 atexit 回调链 → cleanup() → log_deinit()。
 *
 * @param argc  命令行参数个数
 * @param argv  命令行参数数组
 * @return      0 正常退出，非零异常退出
 */
int main(int argc, char *argv[])
{
    /* 注册清理回调（必须最先注册，确保最后执行） */
    atexit(cleanup);

    /* 初始化信号处理（SIGINT/SIGTERM → exit(0) → atexit 链） */
    app_signal_init();

    /* 初始化日志模块 */
    hw_err_t ret = log_init("/tmp/project.log", LOG_DEBUG);
    if (ret != HW_OK) {
        return 1;
    }

    LOG_INFO("Application started");
    LOG_INFO("Platform: RK3588 Orange Pi 5 Plus");

    /* 初始化 SHT30 温湿度传感器 */
    g_sht30 = sht30_open(SHT30_SYSFS_PATH);
    if (!g_sht30) {
        LOG_ERROR("Failed to open SHT30 sensor");
        return 1;
    }
    LOG_INFO("SHT30 sensor initialized at %s", SHT30_SYSFS_PATH);

    /* 主循环 */
    while (1) {
        float temp_c = 0.0f;
        if (sht30_read_temperature(g_sht30, &temp_c) == HW_OK) {
            LOG_INFO("Temperature: %.1f °C", temp_c);
        } else {
            LOG_ERROR("Failed to read SHT30 temperature");
        }
        sleep(1);
    }

    return 0;
}
