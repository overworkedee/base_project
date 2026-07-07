/**
 * @file dev_sht30.c
 * @brief SHT30 温湿度传感器 sysfs 读取实现
 *
 * 通过内核驱动暴露的 sysfs 接口读取温湿度数据：
 *   <sysfs_path>/temperature  — 温度（°C）
 *   <sysfs_path>/humidity     — 湿度（%RH）
 *
 * 内部使用 hw_mutex_t 保证所有 sysfs 操作的线程安全。
 */

#include "hw/dev/dev_sht30.h"
#include "hw/hw_mutex.h"
#include "log/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* ── 内部结构 ─────────────────────────────────────────────────────── */

struct sht30_ctx {
    char        path[256];   /* sysfs 基路径，如 "/sys/bus/i2c/devices/2-0044" */
    hw_mutex_t  lock;        /* 线程安全锁                              */
};

/* ── 内部辅助函数 ─────────────────────────────────────────────────── */

/**
 * 从 sysfs 文件读取一个浮点数。
 *
 * @param path   sysfs 文件完整路径
 * @param value  输出参数，接收读取的浮点值
 * @return       HW_OK 成功，HW_ERR_IO 失败，HW_ERR_PARAM 参数非法
 */
static hw_err_t sht30_read_sysfs_float(const char* path, float* value)
{
    if (!path || !value) return HW_ERR_PARAM;

    FILE* f = fopen(path, "r");
    if (!f) {
        LOG_ERROR("fopen(%s) read failed: %s", path, strerror(errno));
        return HW_ERR_IO;
    }

    if (fscanf(f, "%f", value) != 1) {
        LOG_ERROR("fscanf(%s) failed: %s", path, strerror(errno));
        fclose(f);
        return HW_ERR_IO;
    }

    fclose(f);
    return HW_OK;
}

/* ── 公开 API ─────────────────────────────────────────────────────── */

/**
 * 打开 SHT30 温湿度传感器。
 *
 * 校验 sysfs 路径下 temperature 文件是否存在，分配句柄并初始化互斥锁。
 *
 * @param sysfs_path  sysfs 设备基路径，如 "/sys/bus/i2c/devices/2-0044"
 * @return            成功返回 SHT30 句柄，失败返回 NULL
 */
sht30_t* sht30_open(const char* sysfs_path)
{
    if (!sysfs_path) return NULL;

    /* 校验 sysfs 路径下 temperature 文件存在 */
    char path[256];
    snprintf(path, sizeof(path), "%s/temperature", sysfs_path);
    if (access(path, R_OK) != 0) {
        LOG_ERROR("sensor not accessible at %s: %s", path, strerror(errno));
        return NULL;
    }

    /* 分配句柄 */
    sht30_t* sht30 = (sht30_t*)calloc(1, sizeof(sht30_t));
    if (!sht30) {
        LOG_ERROR("calloc failed for path '%s'", sysfs_path);
        return NULL;
    }
    strncpy(sht30->path, sysfs_path, sizeof(sht30->path) - 1);
    sht30->path[sizeof(sht30->path) - 1] = '\0';

    /* 初始化互斥锁 */
    hw_err_t ret = hw_mutex_init(&sht30->lock);
    if (ret != HW_OK) {
        LOG_ERROR("mutex init failed: %s", hw_err_str(ret));
        free(sht30);
        return NULL;
    }

    return sht30;
}

/**
 * 关闭 SHT30 设备并释放所有资源。
 *
 * @param sht30  SHT30 句柄，可为 NULL
 */
void sht30_close(sht30_t* sht30)
{
    if (!sht30) return;
    hw_mutex_destroy(&sht30->lock);
    free(sht30);
}

/**
 * 读取当前温度值（°C）。
 *
 * @param sht30   SHT30 句柄
 * @param temp_c  输出参数，接收温度值（°C）
 * @return        HW_OK 成功，HW_ERR_PARAM 或 HW_ERR_IO 失败
 * @note          线程安全
 */
hw_err_t sht30_read_temperature(sht30_t* sht30, float* temp_c)
{
    if (!sht30 || !temp_c) return HW_ERR_PARAM;

    hw_err_t ret = hw_mutex_lock(&sht30->lock);
    if (ret != HW_OK) return ret;

    char path[256];
    snprintf(path, sizeof(path), "%s/temperature", sht30->path);
    ret = sht30_read_sysfs_float(path, temp_c);

    hw_mutex_unlock(&sht30->lock);
    return ret;
}

/**
 * 读取当前湿度值（%RH）。
 *
 * @param sht30      SHT30 句柄
 * @param humidity   输出参数，接收湿度值（%RH）
 * @return           HW_OK 成功，HW_ERR_PARAM 或 HW_ERR_IO 失败
 * @note             线程安全
 */
hw_err_t sht30_read_humidity(sht30_t* sht30, float* humidity)
{
    if (!sht30 || !humidity) return HW_ERR_PARAM;

    hw_err_t ret = hw_mutex_lock(&sht30->lock);
    if (ret != HW_OK) return ret;

    char path[256];
    snprintf(path, sizeof(path), "%s/humidity", sht30->path);
    ret = sht30_read_sysfs_float(path, humidity);

    hw_mutex_unlock(&sht30->lock);
    return ret;
}
