/**
 * cmd_protocol.c -- 二进制帧协议的组包与拆包实现
 *
 * 帧格式: HEAD(2B) + LEN(2B big-endian) + CMD(1B) + SUB(1B) + PAYLOAD(LEN B) + CRC(1B)
 * CRC 覆盖范围: HEAD 到 PAYLOAD 末字节
 */

#include "cmd/cmd_protocol.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>  /* htons, ntohs */

/* ── 内部常量 ───────────────────────────────────────────────────────── */

#define FRAME_TOTAL_OVERHEAD  (CMD_FRAME_HEADER_SIZE + 1)  /* header + CRC = 7 */

/* ── 组包 ───────────────────────────────────────────────────────────── */

/**
 * 将 cmd_frame_t 序列化为二进制帧写入 buf。
 *
 * 帧格式: HEAD(0xA5 0x5A) + LEN(2B BE) + CMD(1B) + SUB(1B) + PAYLOAD + CRC(XOR)
 *
 * @param frame    待打包的帧
 * @param buf      输出缓冲区
 * @param buf_cap  输出缓冲区容量
 * @param out_len  输出参数，实际写入的字节数
 * @return         0 成功，-1 缓冲区不足
 */
int cmd_protocol_pack(const cmd_frame_t* frame, uint8_t* buf, size_t buf_cap,
                      size_t* out_len)
{
    size_t total = FRAME_TOTAL_OVERHEAD + frame->len;
    if (total > buf_cap) return -1;

    size_t pos = 0;

    /* HEAD */
    buf[pos++] = CMD_FRAME_HEAD_MAGIC;
    buf[pos++] = CMD_FRAME_TAIL_MAGIC;

    /* LEN (big-endian) */
    uint16_t len_be = htons(frame->len);
    memcpy(buf + pos, &len_be, 2);
    pos += 2;

    /* CMD */
    buf[pos++] = frame->cmd;

    /* SUB */
    buf[pos++] = frame->sub;

    /* PAYLOAD */
    if (frame->len > 0 && frame->payload) {
        memcpy(buf + pos, frame->payload, frame->len);
        pos += frame->len;
    }

    /* CRC (over HEAD through PAYLOAD) */
    buf[pos] = cmd_crc8(buf, pos);
    pos++;

    if (out_len) *out_len = pos;
    return 0;
}

/* ── 拆包 ───────────────────────────────────────────────────────────── */

/**
 * 从字节流中尝试解析一帧（帧同步 + CRC 校验）。
 *
 * 支持跳过前导非 0xA5 字节和错误帧。CRC 不匹配时跳过 1 字节重试。
 *
 * @param data      输入字节流
 * @param data_len  可用字节数
 * @param frame     输出，解析出的帧（payload 由 malloc 分配）
 * @param consumed  输出，实际消费的字节数
 * @return          0=完整帧  1=数据不足  -1=校验失败
 */
int cmd_protocol_parse(const uint8_t* data, size_t data_len, cmd_frame_t* frame,
                       size_t* consumed)
{
    *consumed = 0;
    if (!data || data_len == 0 || !frame) return 1;

    size_t idx = 0;

    /* 扫描帧头 0xA5 0x5A */
    while (idx < data_len) {
        if (data[idx] == CMD_FRAME_HEAD_MAGIC) {
            if (idx + 1 < data_len && data[idx + 1] == CMD_FRAME_TAIL_MAGIC) {
                break;  /* 找到帧头 */
            }
        }
        idx++;
    }

    /* 没找到帧头 */
    if (idx >= data_len) {
        *consumed = 0;
        return 1;  /* 需要更多数据 */
    }

    /* idx 可能已经跳过了一些垃圾字节，标记为已消费 */
    size_t skipped = idx;
    *consumed = skipped;

    /* 检查是否有足够字节读取 HEADER */
    if (data_len - idx < CMD_FRAME_HEADER_SIZE) {
        *consumed = 0;  /* 回退：当前 0xA5 开头的帧还不完整 */
        return 1;
    }

    /* 读取 LEN (big-endian) */
    uint16_t len_be;
    memcpy(&len_be, data + idx + 2, 2);
    uint16_t payload_len = ntohs(len_be);

    /* 检查总帧长度是否超出协议限制 */
    size_t total = FRAME_TOTAL_OVERHEAD + payload_len;
    if (data_len - idx < total) {
        *consumed = 0;  /* 回退：帧数据不完整 */
        return 1;
    }

    /* 校验 CRC */
    size_t crc_cover = CMD_FRAME_HEADER_SIZE + payload_len;
    uint8_t expected = cmd_crc8(data + idx, crc_cover);
    uint8_t received = data[idx + crc_cover];
    if (expected != received) {
        /* CRC 不匹配，消费 1 字节后重新同步 */
        *consumed = skipped + 1;
        return -1;
    }

    /* 填充 frame 结构 */
    frame->cmd = data[idx + 4];
    frame->sub = data[idx + 5];
    frame->len = payload_len;
    if (payload_len > 0) {
        frame->payload = (uint8_t*)malloc(payload_len);
        if (!frame->payload) {
            *consumed = 0;
            return 1;  /* 内存不足，下次重试 */
        }
        memcpy(frame->payload, data + idx + CMD_FRAME_HEADER_SIZE, payload_len);
    } else {
        frame->payload = NULL;
    }

    *consumed = skipped + total;
    return 0;
}

/* ── 释放 ───────────────────────────────────────────────────────────── */

/**
 * 释放 cmd_protocol_parse 分配的 payload 内存。
 *
 * @param frame  待释放的帧（重复调用安全）
 */
void cmd_protocol_free_frame(cmd_frame_t* frame)
{
    if (frame && frame->payload) {
        free(frame->payload);
        frame->payload = NULL;
    }
}
