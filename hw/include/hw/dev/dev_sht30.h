#ifndef DEV_SHT30_H
#define DEV_SHT30_H

#include <stddef.h>
#include "hw/hw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 类型定义 ─────────────────────────────────────────────────────── */

/** 不透明 SHT30 句柄 */
typedef struct sht30_ctx sht30_t;

/* ── 生命周期 ─────────────────────────────────────────────────────── */

/**
 * 打开 SHT30 温湿度传感器。
 *
 * 校验 sysfs 路径下 temperature 和 humidity 文件是否存在，
 * 分配句柄并初始化互斥锁。
 *
 * @param sysfs_path  sysfs 设备基路径，如 "/sys/bus/i2c/devices/2-0044"
 * @return            成功返回 SHT30 句柄，失败返回 NULL
 */
sht30_t* sht30_open(const char* sysfs_path);

/**
 * 关闭 SHT30 设备并释放所有资源。
 *
 * @param sht30  SHT30 句柄，可为 NULL（此时无操作）
 */
void sht30_close(sht30_t* sht30);

/* ── 数据读取 ─────────────────────────────────────────────────────── */

/**
 * 读取当前温度值。
 *
 * 从 <sysfs_path>/temperature 文件读取，单位为摄氏度（°C）。
 *
 * @param sht30   SHT30 句柄
 * @param temp_c  输出参数，接收温度值（°C）
 * @return        HW_OK 成功，HW_ERR_PARAM（参数为 NULL），HW_ERR_IO（读取失败）
 * @note          线程安全
 */
hw_err_t sht30_read_temperature(sht30_t* sht30, float* temp_c);

/**
 * 读取当前湿度值。
 *
 * 从 <sysfs_path>/humidity 文件读取，单位为相对湿度百分比（%RH）。
 *
 * @param sht30      SHT30 句柄
 * @param humidity   输出参数，接收湿度值（%RH）
 * @return           HW_OK 成功，HW_ERR_PARAM（参数为 NULL），HW_ERR_IO（读取失败）
 * @note             线程安全
 */
hw_err_t sht30_read_humidity(sht30_t* sht30, float* humidity);

#ifdef __cplusplus
}
#endif

#endif /* DEV_SHT30_H */
