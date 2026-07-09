#ifndef CMD_PROTOCOL_H
#define CMD_PROTOCOL_H

#include "cmd/cmd_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 将 cmd_frame_t 序列化为二进制帧写入 buf。
 *
 * 格式化: HEAD(2B) + LEN(2B) + CMD(1B) + SUB(1B) + PAYLOAD(LEN B) + CRC(1B)
 * LEN 使用大端序。
 *
 * @param frame      待打包的帧（frame->payload 可为 NULL 当 frame->len==0）
 * @param buf        输出缓冲区
 * @param buf_cap    输出缓冲区容量
 * @param out_len    输出参数，实际写入的字节数
 * @return           0 成功，-1 缓冲区不足
 */
int cmd_protocol_pack(const cmd_frame_t* frame, uint8_t* buf, size_t buf_cap,
                      size_t* out_len);

/**
 * 从字节流中尝试解析一帧。
 *
 * 实现帧同步策略：扫描 0xA5 0x5A → 读 LEN → 读 PAYLOAD+CRC → 校验。
 *
 * @param data       输入字节流
 * @param data_len   输入字节流长度
 * @param frame      输出参数，解析出的帧（payload 由 malloc 分配，调用者需通过
 *                   cmd_protocol_free_frame 释放）
 * @param consumed   输出参数，从 data 起始成功消费的字节数（含跳过的字节）
 * @return           0 成功解析一帧
 *                   1 数据不完整，需要更多字节（consumed 设为 0）
 *                  -1 帧校验失败或格式错误，已消费 1 字节（从下一个 0xA5 重新同步）
 */
int cmd_protocol_parse(const uint8_t* data, size_t data_len, cmd_frame_t* frame,
                       size_t* consumed);

/**
 * 释放 cmd_protocol_parse 分配的帧资源。
 *
 * @param frame  待释放的帧
 */
void cmd_protocol_free_frame(cmd_frame_t* frame);

#ifdef __cplusplus
}
#endif

#endif /* CMD_PROTOCOL_H */
