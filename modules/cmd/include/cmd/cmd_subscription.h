#ifndef CMD_SUBSCRIPTION_H
#define CMD_SUBSCRIPTION_H

#include "cmd/cmd_frame.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 前向声明 */
typedef struct cmd_conn cmd_conn_t;
typedef struct cmd_subscription_mgr cmd_subscription_mgr_t;

/**
 * 创建订阅管理器实例。
 *
 * @return 成功返回实例指针，失败返回 NULL
 */
cmd_subscription_mgr_t* cmd_subscription_create(void);

/**
 * 销毁订阅管理器并释放所有资源。
 *
 * @param mgr  订阅管理器实例
 */
void cmd_subscription_destroy(cmd_subscription_mgr_t* mgr);

/**
 * 添加一个订阅。
 *
 * 同一个 (data_id, conn) 重复添加视为更新 interval_ms。
 *
 * @param mgr          订阅管理器
 * @param data_id      数据流 ID（如 CMD_DATA_TEMPERATURE）
 * @param interval_ms  推送间隔（毫秒），0 表示事件驱动
 * @param conn         订阅者连接
 * @return             0 成功，-1 失败
 */
int cmd_subscription_add(cmd_subscription_mgr_t* mgr, uint16_t data_id,
                         uint32_t interval_ms, cmd_conn_t* conn);

/**
 * 移除一个订阅。
 *
 * @param mgr      订阅管理器
 * @param data_id  数据流 ID
 * @param conn     订阅者连接
 * @return         0 成功，-1 未找到
 */
int cmd_subscription_remove(cmd_subscription_mgr_t* mgr, uint16_t data_id,
                            cmd_conn_t* conn);

/**
 * 向所有订阅了指定 data_id 的连接推送数据帧。
 *
 * 内部组推送帧（CMD=SENSOR, SUB=响应|订阅, PAYLOAD=[data_id 2B BE, value LEN B]）
 * 并通过 cmd_conn_send 发送给每个订阅者。
 *
 * @param mgr        订阅管理器
 * @param cmd        命令大类（用于组帧，通常为 CMD_SENSOR）
 * @param data_id    数据流 ID
 * @param value      数据值（已为大端序）
 * @param value_len  数据值长度（字节）
 * @return           成功推送的连接数
 */
int cmd_subscription_push(cmd_subscription_mgr_t* mgr, uint8_t cmd,
                          uint16_t data_id, const uint8_t* value,
                          size_t value_len);

/**
 * 移除指定连接上的所有订阅（连接关闭时调用）。
 *
 * @param mgr   订阅管理器
 * @param conn  待清理的连接
 */
void cmd_subscription_remove_all(cmd_subscription_mgr_t* mgr, cmd_conn_t* conn);

#ifdef __cplusplus
}
#endif

#endif /* CMD_SUBSCRIPTION_H */
