#ifndef CMD_FRAME_H
#define CMD_FRAME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 帧常量 ─────────────────────────────────────────────────────────── */

#define CMD_FRAME_HEAD_MAGIC   0xA5  /* 帧头第一字节         */
#define CMD_FRAME_TAIL_MAGIC   0x5A  /* 帧头第二字节         */
#define CMD_FRAME_HEADER_SIZE  6     /* HEAD+LEN+CMD+SUB     */
#define CMD_FRAME_MAX_PAYLOAD  65535 /* 最大负载 64KB        */

/* ── 子命令 bit 编码 ────────────────────────────────────────────────── */

#define CMD_SUB_RESPONSE_FLAG  0x80  /* bit7: 0=请求 1=响应/推送 */

/**
 * 从请求子命令生成对应的响应子命令（置 bit7）。
 *
 * @param sub  请求子命令码
 * @return     响应子命令码（sub | 0x80）
 */
static inline uint8_t cmd_frame_sub_rsp(uint8_t sub) { return sub | CMD_SUB_RESPONSE_FLAG; }

/**
 * 获取请求子命令码（清除 bit7）。
 *
 * @param sub  请求或响应子命令码
 * @return     纯操作码（低 7 位）
 */
static inline uint8_t cmd_frame_sub_req(uint8_t sub) { return sub & ~CMD_SUB_RESPONSE_FLAG; }

/* ── 命令大类 ───────────────────────────────────────────────────────── */

#define CMD_LED     0x01  /* LED 读写控制           */
#define CMD_SENSOR  0x02  /* 传感器读写 + 数据流订阅 */
#define CMD_SYSTEM  0x03  /* 系统信息/日志等级       */

/* ── 子命令操作码 ───────────────────────────────────────────────────── */

#define CMD_SUB_WRITE       0x01  /* 写操作         */
#define CMD_SUB_READ        0x02  /* 读操作         */
#define CMD_SUB_SUBSCRIBE   0x03  /* 订阅数据流     */
#define CMD_SUB_UNSUBSCRIBE 0x04  /* 取消订阅       */

/* ── 数据流 ID ──────────────────────────────────────────────────────── */

#define CMD_DATA_TEMPERATURE  0x0001  /* 温度 °C       */
#define CMD_DATA_HUMIDITY     0x0002  /* 湿度 %RH      */

/* ── 错误码 ─────────────────────────────────────────────────────────── */

#define CMD_ERR_OK              0x00  /* 成功                     */
#define CMD_ERR_UNKNOWN_CMD     0x01  /* 未知命令大类             */
#define CMD_ERR_UNKNOWN_SUB     0x02  /* 未知子命令               */
#define CMD_ERR_PARAM           0x03  /* 参数非法（长度/取值）    */
#define CMD_ERR_HARDWARE        0x04  /* 硬件操作失败             */
#define CMD_ERR_BUSY            0x05  /* 资源忙（锁被占用超时）   */

/* ── 帧结构 ─────────────────────────────────────────────────────────── */

/**
 * 解析后的命令帧。
 *
 * 由 protocol_parse 填充，调用者负责通过 cmd_protocol_free_frame 释放 payload。
 */
typedef struct {
    uint8_t  cmd;      /* 命令大类                     */
    uint8_t  sub;      /* 子命令（含方向位）            */
    uint16_t len;      /* payload 长度                 */
    uint8_t* payload;  /* 变长负载（malloc 分配）       */
} cmd_frame_t;

/* ── CRC ────────────────────────────────────────────────────────────── */

/**
 * 计算 XOR 校验和（8-bit CRC）。
 *
 * @param data  数据起始地址
 * @param len   数据长度
 * @return      XOR 校验和（单字节）
 */
static inline uint8_t cmd_crc8(const uint8_t* data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
    }
    return crc;
}

#ifdef __cplusplus
}
#endif

#endif /* CMD_FRAME_H */
