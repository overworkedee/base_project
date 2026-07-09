# 命令模块 (cmd) 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 project_app 构建分层命令模块，支持 Unix Socket/TCP 双传输、二进制帧协议、请求-响应和订阅推送。

**Architecture:** 5 层解耦 — 传输层 (socket accept/read/write) → 协议层 (帧组包/拆包/CRC) → 服务层 (epoll 事件循环+连接管理) → 调度层 (CMD→handler 路由+订阅管理) → 处理器 (LED/Sensor/System)。混合并发：主线程跑 epoll，传感器工作线程定时采集并通过 subscription_push 触发推送。

**Tech Stack:** C11, pthread, epoll, Unix Domain Socket, TCP Socket

## 全局约束

- 所有 LOG_* 日志消息必须使用英文
- 所有函数使用中文注释，遵循 `@param/@return/@note` 格式
- 所有公开头文件遵循 `#ifndef` 头文件保护惯例
- 错误码使用项目已有的 `hw_err_t` 体系（来自 `hw/hw_error.h`）
- 线程安全通过 `hw_mutex.h` 接口实现
- 模块放在 `modules/cmd/` 下，构建系统为 CMake
- 跨平台编译：ARM64 RK3588 (aarch64-none-linux-gnu-)

---

### Task 1: 帧结构定义和 CRC 工具 (cmd_frame.h)

**Files:**
- Create: `modules/cmd/include/cmd/cmd_frame.h`

**Interfaces:**
- Produces: 所有常量宏 (`CMD_FRAME_HEAD_*`, `CMD_*`)，帧结构 `cmd_frame_t`，CRC 函数 `cmd_crc8()`，辅助函数 `cmd_frame_sub_req/rsp()`

**目的:** 定义全模块共享的数据结构和常量，零依赖，可被所有后续任务直接 include 使用。

- [ ] **Step 1: 编写 cmd_frame.h 完整内容**

```c
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
```

- [ ] **Step 2: 验证编译**

```bash
gcc -std=c11 -c -I modules/cmd/include -o /dev/null modules/cmd/include/cmd/cmd_frame.h
```

Expected: 成功，无错误。

- [ ] **Step 3: 提交**

```bash
git add modules/cmd/include/cmd/cmd_frame.h
git commit -m "feat(cmd): add frame constants, structures, and CRC utility

- Define binary frame protocol constants (HEAD magic, max payload)
- Define command class codes (LED/Sensor/System) and sub-command opcodes
- Define error codes and data stream IDs
- Add cmd_frame_t structure and cmd_crc8 XOR checksum function

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 2: 协议层 — 组包与拆包 (cmd_protocol)

**Files:**
- Create: `modules/cmd/include/cmd/cmd_protocol.h`
- Create: `modules/cmd/src/cmd_protocol.c`

**Interfaces:**
- Consumes: `cmd_frame_t`, `cmd_crc8()`, `CMD_FRAME_HEADER_SIZE`, `CMD_FRAME_HEAD_MAGIC`, `CMD_FRAME_TAIL_MAGIC` from `cmd_frame.h`
- Produces: `cmd_protocol_pack()`, `cmd_protocol_parse()`, `cmd_protocol_free_frame()`

**目的:** 将帧结构序列化为字节流（组包），从字节流中提取完整帧（拆包，含帧同步）。

- [ ] **Step 1: 编写 cmd_protocol.h**

```c
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
```

- [ ] **Step 2: 编写 cmd_protocol.c**

```c
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

void cmd_protocol_free_frame(cmd_frame_t* frame)
{
    if (frame && frame->payload) {
        free(frame->payload);
        frame->payload = NULL;
    }
}
```

- [ ] **Step 3: 验证编译（仅目标文件，不链接）**

```bash
gcc -std=c11 -c -I modules/cmd/include -I modules/log/include -I hw/include \
    -o /tmp/cmd_protocol.o modules/cmd/src/cmd_protocol.c
```

Expected: 成功，无错误。

- [ ] **Step 4: 提交**

```bash
git add modules/cmd/include/cmd/cmd_protocol.h modules/cmd/src/cmd_protocol.c
git commit -m "feat(cmd): add protocol layer — pack, parse, and CRC validation

- cmd_protocol_pack: serialize cmd_frame_t to binary buffer with header/CRC
- cmd_protocol_parse: scan 0xA5 0x5A sync word, extract frame, validate CRC
- Returns 0 (complete), 1 (need more data), -1 (sync error, skip 1 byte)
- LEN field uses network byte order (big-endian) via htons/ntohs

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 3: 协议层单元测试 (test_cmd_protocol)

**Files:**
- Create: `modules/cmd/tests/test_cmd_protocol.c`

**Interfaces:**
- Consumes: `cmd_protocol_pack()`, `cmd_protocol_parse()`, `cmd_protocol_free_frame()`, `cmd_frame_t`

**目的:** TDD — 先用测试验证协议层的组包/拆包/CRC/粘包/错误恢复逻辑正确。

- [ ] **Step 1: 编写测试用例**

```c
/**
 * test_cmd_protocol.c -- 协议层单元测试
 *
 * 覆盖: 正常组包拆包、空 payload、CRC 校验失败、数据不完整（粘包）、
 *       帧同步跳过垃圾字节、连续多帧解析、大端序 LEN 正确性。
 */

#include "cmd/cmd_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { printf("  TEST %s ... ", name); } while(0)

#define PASS() \
    do { printf("PASS\n"); tests_passed++; } while(0)

#define FAIL(msg) \
    do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

/* ── 辅助函数 ───────────────────────────────────────────────────────── */

/** 打印十六进制，辅助调试 */
static void hex_dump(const char* label, const uint8_t* data, size_t len) {
    printf("    %s [%zu]: ", label, len);
    for (size_t i = 0; i < len; i++) printf("%02X ", data[i]);
    printf("\n");
}

/* ── 测试用例 ───────────────────────────────────────────────────────── */

/** 测试 1: 组包后拆包，验证往返一致性 */
static void test_pack_unpack_roundtrip(void) {
    TEST("pack/unpack roundtrip");

    /* 构造帧 */
    cmd_frame_t in = {
        .cmd = CMD_LED,
        .sub = CMD_SUB_WRITE,
        .len = 2,
    };
    uint8_t pld[] = {0x01, 0x01};  /* LED#1 开 */
    in.payload = pld;

    /* 组包 */
    uint8_t buf[256];
    size_t out_len = 0;
    int rc = cmd_protocol_pack(&in, buf, sizeof(buf), &out_len);
    if (rc != 0) { FAIL("pack failed"); return; }
    if (out_len != 9) { FAIL("wrong packed length"); return; }

    /* 拆包 */
    cmd_frame_t out;
    memset(&out, 0, sizeof(out));
    size_t consumed = 0;
    rc = cmd_protocol_parse(buf, out_len, &out, &consumed);
    if (rc != 0) { FAIL("parse failed"); return; }
    if (consumed != 9) { FAIL("wrong consumed"); goto cleanup; }
    if (out.cmd != CMD_LED) { FAIL("cmd mismatch"); goto cleanup; }
    if (out.sub != CMD_SUB_WRITE) { FAIL("sub mismatch"); goto cleanup; }
    if (out.len != 2) { FAIL("len mismatch"); goto cleanup; }
    if (memcmp(out.payload, pld, 2) != 0) { FAIL("payload mismatch"); goto cleanup; }

    PASS();
cleanup:
    cmd_protocol_free_frame(&out);
}

/** 测试 2: 空 payload 帧 */
static void test_empty_payload(void) {
    TEST("empty payload");

    cmd_frame_t in = { .cmd = CMD_SENSOR, .sub = CMD_SUB_READ, .len = 0, .payload = NULL };

    uint8_t buf[256];
    size_t out_len = 0;
    int rc = cmd_protocol_pack(&in, buf, sizeof(buf), &out_len);
    if (rc != 0) { FAIL("pack failed"); return; }
    if (out_len != 7) { FAIL("wrong packed length"); return; }  /* 6 header + 1 CRC */

    cmd_frame_t out;
    memset(&out, 0, sizeof(out));
    size_t consumed = 0;
    rc = cmd_protocol_parse(buf, out_len, &out, &consumed);
    if (rc != 0) { FAIL("parse failed"); return; }
    if (out.len != 0) { FAIL("len should be 0"); goto cleanup; }
    if (out.payload != NULL) { FAIL("payload should be NULL"); goto cleanup; }

    PASS();
cleanup:
    cmd_protocol_free_frame(&out);
}

/** 测试 3: CRC 校验失败应返回 -1 并消费 1 字节 */
static void test_crc_mismatch(void) {
    TEST("CRC mismatch");

    cmd_frame_t in = { .cmd = CMD_SYSTEM, .sub = CMD_SUB_READ, .len = 0, .payload = NULL };

    uint8_t buf[256];
    size_t out_len = 0;
    cmd_protocol_pack(&in, buf, sizeof(buf), &out_len);

    /* 破坏 CRC（最后一字节） */
    buf[out_len - 1] ^= 0xFF;

    cmd_frame_t out;
    memset(&out, 0, sizeof(out));
    size_t consumed = 0;
    int rc = cmd_protocol_parse(buf, out_len, &out, &consumed);
    if (rc != -1) { FAIL("should return -1 for CRC mismatch"); return; }
    if (consumed != 1) { FAIL("should consume 1 byte on CRC error"); return; }

    PASS();
    cmd_protocol_free_frame(&out);
}

/** 测试 4: 数据不完整 — 分两次收到一帧 */
static void test_split_frame(void) {
    TEST("split frame (incomplete data)");

    cmd_frame_t in = { .cmd = CMD_SENSOR, .sub = CMD_SUB_READ, .len = 0, .payload = NULL };

    uint8_t buf[256];
    size_t out_len = 0;
    cmd_protocol_pack(&in, buf, sizeof(buf), &out_len);
    /* out_len = 7 */

    /* 第一次只给 3 字节 */
    cmd_frame_t out;
    memset(&out, 0, sizeof(out));
    size_t consumed = 0;
    int rc = cmd_protocol_parse(buf, 3, &out, &consumed);
    if (rc != 1) { FAIL("should return 1 for incomplete data"); return; }
    if (consumed != 0) { FAIL("consumed should be 0 on incomplete"); return; }

    /* 第二次给完整数据 */
    rc = cmd_protocol_parse(buf, out_len, &out, &consumed);
    if (rc != 0) { FAIL("parse should succeed with full data"); return; }

    PASS();
    cmd_protocol_free_frame(&out);
}

/** 测试 5: 连续两帧粘在一起 */
static void test_back_to_back_frames(void) {
    TEST("back-to-back frames");

    cmd_frame_t f1 = { .cmd = CMD_LED, .sub = CMD_SUB_WRITE, .len = 2 };
    uint8_t pld1[] = {0x01, 0x01};
    f1.payload = pld1;

    cmd_frame_t f2 = { .cmd = CMD_SENSOR, .sub = CMD_SUB_READ, .len = 0, .payload = NULL };

    uint8_t buf[256];
    size_t len1 = 0, len2 = 0;
    cmd_protocol_pack(&f1, buf, sizeof(buf), &len1);
    cmd_protocol_pack(&f2, buf + len1, sizeof(buf) - len1, &len2);

    /* 一次性解析两帧 */
    cmd_frame_t out;
    size_t consumed = 0, total = len1 + len2;

    /* 第一帧 */
    memset(&out, 0, sizeof(out));
    int rc = cmd_protocol_parse(buf, total, &out, &consumed);
    if (rc != 0) { FAIL("first frame parse failed"); return; }
    cmd_protocol_free_frame(&out);

    /* 第二帧 */
    memset(&out, 0, sizeof(out));
    size_t consumed2 = 0;
    rc = cmd_protocol_parse(buf + consumed, total - consumed, &out, &consumed2);
    if (rc != 0) { FAIL("second frame parse failed"); return; }
    if (out.cmd != CMD_SENSOR) { FAIL("second frame cmd mismatch"); goto cleanup; }

    PASS();
cleanup:
    cmd_protocol_free_frame(&out);
}

/** 测试 6: 垃圾字节后紧跟有效帧 */
static void test_garbage_before_frame(void) {
    TEST("garbage bytes before frame");

    cmd_frame_t in = { .cmd = CMD_SYSTEM, .sub = CMD_SUB_READ, .len = 0, .payload = NULL };
    uint8_t buf[256];
    size_t out_len = 0;
    cmd_protocol_pack(&in, buf + 3, sizeof(buf) - 3, &out_len);

    /* 前面填垃圾 */
    buf[0] = 0xFF;
    buf[1] = 0x00;
    buf[2] = 0xA5;  /* 单独 A5 不是帧头 */

    cmd_frame_t out;
    memset(&out, 0, sizeof(out));
    size_t consumed = 0;
    int rc = cmd_protocol_parse(buf, 3 + out_len, &out, &consumed);
    if (rc != 0) { FAIL("should find frame after garbage"); return; }
    if (consumed <= 3) { FAIL("should have consumed garbage + frame"); return; }
    if (out.cmd != CMD_SYSTEM) { FAIL("cmd mismatch"); goto cleanup; }

    PASS();
cleanup:
    cmd_protocol_free_frame(&out);
}

/** 测试 7: 大端序 LEN 正确性 */
static void test_big_endian_len(void) {
    TEST("big-endian LEN field");

    cmd_frame_t in = { .cmd = CMD_SENSOR, .sub = CMD_SUB_SUBSCRIBE, .len = 4 };
    uint8_t pld[] = {0x00, 0x01, 0x03, 0xE8};  /* data_id=1, interval=1000 */
    in.payload = pld;

    uint8_t buf[256];
    size_t out_len = 0;
    cmd_protocol_pack(&in, buf, sizeof(buf), &out_len);

    /* 验证 LEN 字段 (buf[2], buf[3]) 为大端序 0x0004 */
    if (buf[2] != 0x00 || buf[3] != 0x04) {
        FAIL("LEN not big-endian");
        return;
    }

    PASS();
}

/** 测试 8: payload 包含大端序 float (25.0°C → 0x41C80000) */
static void test_float_response(void) {
    TEST("float response payload");

    cmd_frame_t in = { .cmd = CMD_SENSOR, .sub = cmd_frame_sub_rsp(CMD_SUB_READ), .len = 4 };
    float temp = 25.0f;
    uint8_t* pld = (uint8_t*)&temp;  /* 注意：依赖主机字节序与大端序一致时需要转换 */
    /* 将 float 转为大端序 */
    uint32_t tmp;
    memcpy(&tmp, &temp, 4);
    tmp = htonl(tmp);  /* 确保大端序 */
    uint8_t be_pld[4];
    memcpy(be_pld, &tmp, 4);
    in.payload = be_pld;

    uint8_t buf[256];
    size_t out_len = 0;
    cmd_protocol_pack(&in, buf, sizeof(buf), &out_len);

    cmd_frame_t out;
    memset(&out, 0, sizeof(out));
    size_t consumed = 0;
    int rc = cmd_protocol_parse(buf, out_len, &out, &consumed);
    if (rc != 0) { FAIL("parse failed"); return; }
    if (out.len != 4) { FAIL("len mismatch"); goto cleanup; }

    /* 将大端序转回 float */
    uint32_t net_val;
    memcpy(&net_val, out.payload, 4);
    uint32_t host_val = ntohl(net_val);
    float result;
    memcpy(&result, &host_val, 4);
    if (result != 25.0f) { FAIL("float value mismatch"); goto cleanup; }

    PASS();
cleanup:
    cmd_protocol_free_frame(&out);
}

/* ── 入口 ───────────────────────────────────────────────────────────── */

int main(void) {
    printf("=== cmd_protocol unit tests ===\n\n");

    test_pack_unpack_roundtrip();
    test_empty_payload();
    test_crc_mismatch();
    test_split_frame();
    test_back_to_back_frames();
    test_garbage_before_frame();
    test_big_endian_len();
    test_float_response();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
```

- [ ] **Step 2: 编译并运行测试（host 环境）**

```bash
gcc -std=c11 -Wall -Wextra \
    -I modules/cmd/include -I modules/log/include -I hw/include \
    -o /tmp/test_cmd_protocol \
    modules/cmd/tests/test_cmd_protocol.c modules/cmd/src/cmd_protocol.c \
    hw/src/hw_error.c
/tmp/test_cmd_protocol
```

Expected: 8/8 PASS，退出码 0。

- [ ] **Step 3: 提交**

```bash
git add modules/cmd/tests/test_cmd_protocol.c
git commit -m "test(cmd): add protocol layer unit tests

8 test cases covering pack/unpack roundtrip, empty payload, CRC mismatch,
split frames, back-to-back frames, garbage bytes before frame,
big-endian LEN, and float value serialization.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 4: 传输层 (cmd_transport)

**Files:**
- Create: `modules/cmd/include/cmd/cmd_transport.h`
- Create: `modules/cmd/src/cmd_transport.c`

**Interfaces:**
- Consumes: 无 cmd 内部依赖（纯 POSIX socket API）
- Produces: `cmd_transport_listen_unix()`, `cmd_transport_listen_tcp()`, `cmd_transport_accept()`, `cmd_transport_close()`

**目的:** 抽象 socket 的创建、绑定、监听和接受，对上层隐藏 Unix Socket 与 TCP 的差异。

- [ ] **Step 1: 编写 cmd_transport.h**

```c
#ifndef CMD_TRANSPORT_H
#define CMD_TRANSPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 传输类型枚举。
 */
typedef enum {
    CMD_TRANSPORT_UNIX = 0,  /* Unix Domain Socket */
    CMD_TRANSPORT_TCP  = 1,  /* TCP Socket         */
} cmd_transport_type_t;

/**
 * 创建 Unix Domain Socket 监听 fd。
 *
 * 自动绑定到 path 并 listen(32)。若 path 已存在则先 unlink。
 *
 * @param path  socket 文件路径，如 "/tmp/cmd.sock"
 * @return      成功返回 fd（≥0），失败返回 -1
 */
int cmd_transport_listen_unix(const char* path);

/**
 * 创建 TCP Socket 监听 fd。
 *
 * 绑定到 0.0.0.0:port 并 listen(32)，设置 SO_REUSEADDR。
 *
 * @param port  监听端口号
 * @return      成功返回 fd（≥0），失败返回 -1
 */
int cmd_transport_listen_tcp(uint16_t port);

/**
 * 接受一个新连接。
 *
 * 对 listen_fd 调用 accept()，阻塞直到有连接到达。
 *
 * @param listen_fd  监听文件描述符
 * @return           成功返回客户端 fd（≥0），失败返回 -1
 */
int cmd_transport_accept(int listen_fd);

/**
 * 关闭 socket。
 *
 * @param fd  待关闭的文件描述符
 */
void cmd_transport_close(int fd);

#ifdef __cplusplus
}
#endif

#endif /* CMD_TRANSPORT_H */
```

- [ ] **Step 2: 编写 cmd_transport.c**

```c
/**
 * cmd_transport.c -- 传输层实现
 *
 * 封装 Unix Domain Socket 和 TCP Socket 的创建/绑定/监听/接受。
 * 内部使用标准 POSIX socket API。
 */

#define _GNU_SOURCE
#include "cmd/cmd_transport.h"
#include "log/log.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define LISTEN_BACKLOG 32

/* ── Unix Socket ────────────────────────────────────────────────────── */

int cmd_transport_listen_unix(const char* path)
{
    if (!path) return -1;

    /* 清理旧的 socket 文件 */
    unlink(path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR("Unix socket create failed: %s", path);
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Unix socket bind failed: %s", path);
        close(fd);
        return -1;
    }

    if (listen(fd, LISTEN_BACKLOG) < 0) {
        LOG_ERROR("Unix socket listen failed: %s", path);
        close(fd);
        return -1;
    }

    LOG_INFO("Unix socket listening on %s (fd=%d)", path, fd);
    return fd;
}

/* ── TCP Socket ─────────────────────────────────────────────────────── */

int cmd_transport_listen_tcp(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR("TCP socket create failed");
        return -1;
    }

    /* 允许端口复用，避免重启后 TIME_WAIT 导致 bind 失败 */
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("TCP bind failed on port %u", port);
        close(fd);
        return -1;
    }

    if (listen(fd, LISTEN_BACKLOG) < 0) {
        LOG_ERROR("TCP listen failed on port %u", port);
        close(fd);
        return -1;
    }

    LOG_INFO("TCP listening on 0.0.0.0:%u (fd=%d)", port, fd);
    return fd;
}

/* ── Accept / Close ─────────────────────────────────────────────────── */

int cmd_transport_accept(int listen_fd)
{
    int client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd < 0) {
        LOG_ERROR("accept failed on fd=%d", listen_fd);
        return -1;
    }
    LOG_INFO("New connection accepted (fd=%d)", client_fd);
    return client_fd;
}

void cmd_transport_close(int fd)
{
    if (fd >= 0) {
        LOG_DEBUG("Closing connection fd=%d", fd);
        close(fd);
    }
}
```

- [ ] **Step 3: 验证编译**

```bash
gcc -std=c11 -c -I modules/cmd/include -I modules/log/include -I hw/include \
    -o /tmp/cmd_transport.o modules/cmd/src/cmd_transport.c
```

Expected: 成功，无错误。

- [ ] **Step 4: 提交**

```bash
git add modules/cmd/include/cmd/cmd_transport.h modules/cmd/src/cmd_transport.c
git commit -m "feat(cmd): add transport layer — Unix and TCP socket listeners

- cmd_transport_listen_unix: create/bind/listen on Unix domain socket
- cmd_transport_listen_tcp: create/bind/listen on TCP socket with SO_REUSEADDR
- cmd_transport_accept / cmd_transport_close: accept connection, close socket

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 5: 服务层 — 连接管理和 epoll 事件循环 (cmd_server)

**Files:**
- Create: `modules/cmd/include/cmd/cmd_server.h`
- Create: `modules/cmd/src/cmd_server.c`

**Interfaces:**
- Consumes: `cmd_transport_*`, `cmd_protocol_parse/pack/free_frame`, `cmd_frame_t`
- Produces: `cmd_server_create/destroy()`, `cmd_server_add_listener()`, `cmd_server_set_handler()`, `cmd_server_run/stop()`, `cmd_conn_send()`, `cmd_conn_close()`

**目的:** epoll 事件循环是整个命令模块的心脏。管理连接生命周期、收帧/发帧、超时踢出。

- [ ] **Step 1: 编写 cmd_server.h**

```c
#ifndef CMD_SERVER_H
#define CMD_SERVER_H

#include "cmd/cmd_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 前向声明 */
typedef struct cmd_conn cmd_conn_t;
typedef struct cmd_server cmd_server_t;

/**
 * 请求处理回调类型。
 *
 * 当服务器收到完整帧时调用此回调。回调内可以调用 cmd_conn_send 发送响应。
 *
 * @param req   请求帧（只读，调用结束后 server 会释放）
 * @param conn  来源连接
 */
typedef void (*cmd_request_fn)(const cmd_frame_t* req, cmd_conn_t* conn);

/* ── 服务器生命周期 ─────────────────────────────────────────────────── */

/**
 * 创建命令服务器实例。
 *
 * @return 成功返回实例指针，失败返回 NULL
 */
cmd_server_t* cmd_server_create(void);

/**
 * 销毁服务器并释放所有资源（关闭所有连接和监听 fd）。
 *
 * @param s  服务器实例
 */
void cmd_server_destroy(cmd_server_t* s);

/* ── 配置 ───────────────────────────────────────────────────────────── */

/**
 * 向服务器注册一个监听 fd。
 *
 * 必须至少调用一次（可多次），之后调用 cmd_server_run 进入事件循环。
 *
 * @param s         服务器实例
 * @param listen_fd 由 transport_listen_* 创建的监听 fd
 * @return          0 成功，-1 失败（fd 无效或已达上限）
 */
int cmd_server_add_listener(cmd_server_t* s, int listen_fd);

/**
 * 设置请求处理器回调。
 *
 * @param s       服务器实例
 * @param fn      回调函数，收到完整帧时调用
 */
void cmd_server_set_handler(cmd_server_t* s, cmd_request_fn fn);

/* ── 事件循环 ───────────────────────────────────────────────────────── */

/**
 * 启动 epoll 事件循环（阻塞当前线程）。
 *
 * 循环处理 accept / read（拼帧+解析） / write（tx_queue flush） / 超时踢出。
 * 调用 cmd_server_stop 可从其他线程优雅退出。
 *
 * @param s  服务器实例
 * @return   0 正常退出，-1 错误
 */
int cmd_server_run(cmd_server_t* s);

/**
 * 请求事件循环退出。
 *
 * 可从信号处理器或任意线程调用，会使 cmd_server_run 在下一个 epoll_wait 后返回。
 *
 * @param s  服务器实例
 */
void cmd_server_stop(cmd_server_t* s);

/* ── 连接操作（供 handler 回调使用）─────────────────────────────────── */

/**
 * 向指定连接发送一帧。
 *
 * 内部将 frame 组包后加入连接的 tx_queue，注册 EPOLLOUT 事件。
 *
 * @param conn   目标连接
 * @param frame  待发送的帧（函数内完成组包，调用者可随即释放 frame）
 * @return       0 成功，-1 失败（组包出错或连接已关闭）
 */
int cmd_conn_send(cmd_conn_t* conn, const cmd_frame_t* frame);

/**
 * 主动关闭一条连接。
 *
 * 从 epoll 移除、关闭 fd、释放连接资源。
 *
 * @param s     服务器实例
 * @param conn  待关闭的连接
 */
void cmd_conn_close(cmd_server_t* s, cmd_conn_t* conn);

/**
 * 获取连接关联的用户上下文指针。
 *
 * 允许 handler 和 subscription 层在连接上挂载私有数据。
 *
 * @param conn  连接
 * @return      当前上下文指针（初始为 NULL）
 */
void* cmd_conn_get_ctx(cmd_conn_t* conn);

/**
 * 设置连接关联的用户上下文指针。
 *
 * @param conn  连接
 * @param ctx   上下文指针
 */
void cmd_conn_set_ctx(cmd_conn_t* conn, void* ctx);

#ifdef __cplusplus
}
#endif

#endif /* CMD_SERVER_H */
```

- [ ] **Step 2: 编写 cmd_server.c（第一部分 — 连接结构和服务器结构）**

```c
/**
 * cmd_server.c -- 命令服务器（epoll 事件循环 + 连接管理）
 *
 * 内部结构:
 *   cmd_conn_t     — 单条连接（fd、缓冲区、队列、超时）
 *   cmd_server_t   — 服务器实例（epoll fd、连接表、回调）
 *
 * 并发: epoll 事件循环跑在主线程，cmd_conn_send 可从任意线程调用（tx_queue 有锁保护）。
 */

#define _GNU_SOURCE
#include "cmd/cmd_server.h"
#include "cmd/cmd_protocol.h"
#include "cmd/cmd_transport.h"
#include "log/log.h"
#include "hw/hw_mutex.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>
#include <time.h>

/* ── 常量 ───────────────────────────────────────────────────────────── */

#define MAX_LISTENERS    4     /* 最大监听 fd 数              */
#define MAX_CONNECTIONS  64    /* 最大并发连接数              */
#define MAX_EVENTS       16    /* epoll_wait 单次最大事件数   */
#define RX_BUF_SIZE      4096  /* 接收缓冲区大小              */
#define CONN_TIMEOUT_SEC 60    /* 连接超时秒数                */
#define IDLE_CHECK_MS    10000 /* 空闲连接检查间隔（毫秒）    */

/* ── 连接结构 ───────────────────────────────────────────────────────── */

typedef struct cmd_conn {
    int                 fd;
    int                 transport_type;         /* CMD_TRANSPORT_UNIX 或 CMD_TRANSPORT_TCP */
    uint8_t             rx_buf[RX_BUF_SIZE];    /* 接收环形缓冲区                         */
    size_t              rx_len;                 /* 当前已缓存字节数                       */

    /* 发送队列（链表节点） */
    struct tx_node {
        uint8_t*        data;
        size_t          len;
        size_t          sent;
        struct tx_node* next;
    } *tx_head, *tx_tail;
    hw_mutex_t          tx_lock;                /* tx_queue 锁（其他线程可能写入）         */

    int                 epoll_fd;               /* 所属 epoll 实例（用于 EPOLLOUT 注册）    */
    time_t              last_active;            /* 最后活跃时间戳                         */
    void*               ctx;                    /* 用户上下文指针                         */

    struct cmd_conn*    next;                   /* 服务器连接链表指针                     */
} cmd_conn_t;

/* ── 服务器结构 ─────────────────────────────────────────────────────── */

typedef struct cmd_server {
    int                 epoll_fd;               /* epoll 实例                               */
    int                 listener_fds[MAX_LISTENERS];
    int                 listener_count;

    cmd_conn_t*         conn_head;              /* 连接链表头                               */
    int                 conn_count;

    cmd_request_fn      handler;                /* 请求回调                                 */
    volatile int        running;                /* 运行标志（cmd_server_stop 写入）          */
} cmd_server_t;

/* ── 内部辅助：时间戳 ───────────────────────────────────────────────── */

static time_t _now(void) { return time(NULL); }
```

- [ ] **Step 3: 编写 cmd_server.c（第二部分 — 生命周期函数）**

```c
/* ── 服务器生命周期 ─────────────────────────────────────────────────── */

cmd_server_t* cmd_server_create(void)
{
    cmd_server_t* s = (cmd_server_t*)calloc(1, sizeof(cmd_server_t));
    if (!s) return NULL;

    s->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (s->epoll_fd < 0) {
        LOG_ERROR("epoll_create1 failed: %s", strerror(errno));
        free(s);
        return NULL;
    }

    s->running = 0;
    return s;
}

void cmd_server_destroy(cmd_server_t* s)
{
    if (!s) return;

    /* 关闭所有连接 */
    cmd_conn_t* conn = s->conn_head;
    while (conn) {
        cmd_conn_t* next = conn->next;
        cmd_transport_close(conn->fd);
        /* 释放 tx_queue */
        while (conn->tx_head) {
            struct tx_node* node = conn->tx_head;
            conn->tx_head = node->next;
            free(node->data);
            free(node);
        }
        hw_mutex_destroy(&conn->tx_lock);
        free(conn);
        conn = next;
    }

    /* 关闭所有监听 fd */
    for (int i = 0; i < s->listener_count; i++) {
        cmd_transport_close(s->listener_fds[i]);
    }

    if (s->epoll_fd >= 0) close(s->epoll_fd);
    free(s);
}

/* ── 配置 ───────────────────────────────────────────────────────────── */

int cmd_server_add_listener(cmd_server_t* s, int listen_fd)
{
    if (!s || listen_fd < 0 || s->listener_count >= MAX_LISTENERS) return -1;

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events  = EPOLLIN;
    ev.data.fd = listen_fd;

    if (epoll_ctl(s->epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
        LOG_ERROR("epoll_ctl ADD listener fd=%d failed: %s", listen_fd, strerror(errno));
        return -1;
    }

    s->listener_fds[s->listener_count++] = listen_fd;
    return 0;
}

void cmd_server_set_handler(cmd_server_t* s, cmd_request_fn fn)
{
    if (s) s->handler = fn;
}
```

- [ ] **Step 4: 编写 cmd_server.c（第三部分 — epoll 事件循环核心）**

```c
/* ── 内部: 接受新连接 ────────────────────────────────────────────────── */

/**
 * 内部：接受新连接，分配 cmd_conn_t 并注册到 epoll。
 */
static void _accept_conn(cmd_server_t* s, int listen_fd)
{
    if (s->conn_count >= MAX_CONNECTIONS) {
        LOG_WARN("Max connections (%d) reached, rejecting", MAX_CONNECTIONS);
        int fd = cmd_transport_accept(listen_fd);
        if (fd >= 0) cmd_transport_close(fd);
        return;
    }

    int fd = cmd_transport_accept(listen_fd);
    if (fd < 0) return;

    cmd_conn_t* conn = (cmd_conn_t*)calloc(1, sizeof(cmd_conn_t));
    if (!conn) {
        cmd_transport_close(fd);
        return;
    }

    conn->fd = fd;
    conn->rx_len = 0;
    conn->tx_head = conn->tx_tail = NULL;
    conn->last_active = _now();
    conn->ctx = NULL;
    hw_mutex_init(&conn->tx_lock);

    /* 加入 epoll */
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events   = EPOLLIN | EPOLLET;  /* 边缘触发 */
    ev.data.ptr = conn;

    if (epoll_ctl(s->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        LOG_ERROR("epoll_ctl ADD conn fd=%d failed", fd);
        hw_mutex_destroy(&conn->tx_lock);
        free(conn);
        cmd_transport_close(fd);
        return;
    }

    /* 插入连接链表 */
    conn->next = s->conn_head;
    s->conn_head = conn;
    s->conn_count++;

    LOG_DEBUG("Connection registered (fd=%d, total=%d)", fd, s->conn_count);
}

/**
 * 内部：从 epoll 和连接链表移除并释放连接。
 */
static void _close_conn(cmd_server_t* s, cmd_conn_t* conn)
{
    /* 从 epoll 移除 */
    epoll_ctl(s->epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
    cmd_transport_close(conn->fd);

    /* 从链表移除 */
    if (s->conn_head == conn) {
        s->conn_head = conn->next;
    } else {
        cmd_conn_t* prev = s->conn_head;
        while (prev && prev->next != conn) prev = prev->next;
        if (prev) prev->next = conn->next;
    }
    s->conn_count--;

    /* 释放 tx_queue */
    hw_mutex_lock(&conn->tx_lock);
    while (conn->tx_head) {
        struct tx_node* node = conn->tx_head;
        conn->tx_head = node->next;
        free(node->data);
        free(node);
    }
    hw_mutex_unlock(&conn->tx_lock);
    hw_mutex_destroy(&conn->tx_lock);

    LOG_DEBUG("Connection closed (fd=%d, total=%d)", conn->fd, s->conn_count);
    free(conn);
}

/* ── 内部: 数据处理 ─────────────────────────────────────────────────── */

/**
 * 内部：尝试从连接的 rx_buf 中解析一帧。
 * 成功解析后调用 handler 回调，循环直到 rx_buf 无完整帧或出错。
 */
static void _process_rx(cmd_server_t* s, cmd_conn_t* conn)
{
    while (conn->rx_len > 0) {
        cmd_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        size_t consumed = 0;

        int rc = cmd_protocol_parse(conn->rx_buf, conn->rx_len, &frame, &consumed);

        if (rc == 1) {
            /* 数据不完整，等待更多数据 */
            break;
        }

        if (rc == -1) {
            /* 解析错误，丢弃 consumed 字节后继续 */
            LOG_DEBUG("Frame parse error on fd=%d, skipping %zu byte(s)", conn->fd, consumed);
            if (consumed > 0 && consumed <= conn->rx_len) {
                memmove(conn->rx_buf, conn->rx_buf + consumed, conn->rx_len - consumed);
                conn->rx_len -= consumed;
            }
            continue;
        }

        /* rc == 0: 完整帧 */
        conn->last_active = _now();

        if (s->handler) {
            s->handler(&frame, conn);
        } else {
            LOG_WARN("No handler registered, dropping frame cmd=0x%02X", frame.cmd);
        }

        cmd_protocol_free_frame(&frame);

        /* 移除已消费字节 */
        if (consumed <= conn->rx_len) {
            memmove(conn->rx_buf, conn->rx_buf + consumed, conn->rx_len - consumed);
            conn->rx_len -= consumed;
        } else {
            conn->rx_len = 0;
        }
    }

    /* 缓冲区溢出保护 */
    if (conn->rx_len >= RX_BUF_SIZE) {
        LOG_WARN("rx_buf overflow on fd=%d, flushing", conn->fd);
        conn->rx_len = 0;
    }
}

/**
 * 内部：从连接读取数据追加到 rx_buf。
 */
static void _handle_read(cmd_server_t* s, cmd_conn_t* conn)
{
    size_t space = RX_BUF_SIZE - conn->rx_len;
    if (space == 0) {
        _process_rx(s, conn);  /* 尝试消费一些再读 */
        space = RX_BUF_SIZE - conn->rx_len;
    }

    ssize_t n = read(conn->fd, conn->rx_buf + conn->rx_len, space);
    if (n > 0) {
        conn->rx_len += (size_t)n;
        conn->last_active = _now();
        _process_rx(s, conn);
    } else if (n == 0) {
        _close_conn(s, conn);
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_DEBUG("Read error on fd=%d: %s", conn->fd, strerror(errno));
        _close_conn(s, conn);
    }
}

/**
 * 内部：将 tx_queue 中数据写入 socket。
 */
static void _handle_write(cmd_server_t* s, cmd_conn_t* conn)
{
    hw_mutex_lock(&conn->tx_lock);

    while (conn->tx_head) {
        struct tx_node* node = conn->tx_head;
        ssize_t remain = (ssize_t)(node->len - node->sent);
        ssize_t n = write(conn->fd, node->data + node->sent, (size_t)remain);

        if (n > 0) {
            node->sent += (size_t)n;
            conn->last_active = _now();
        }

        if (node->sent >= node->len) {
            /* 此节点发送完毕 */
            conn->tx_head = node->next;
            if (!conn->tx_head) conn->tx_tail = NULL;
            free(node->data);
            free(node);
        } else {
            /* 未发完，等下次 EPOLLOUT */
            break;
        }
    }

    /* 如果全部发完，取消 EPOLLOUT 监听 */
    if (!conn->tx_head) {
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events   = EPOLLIN | EPOLLET;
        ev.data.ptr = conn;
        epoll_ctl(s->epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
    }

    hw_mutex_unlock(&conn->tx_lock);
}

/**
 * 内部：检查并踢出超时连接。
 */
static void _check_idle(cmd_server_t* s)
{
    time_t now = _now();
    cmd_conn_t* conn = s->conn_head;
    while (conn) {
        cmd_conn_t* next = conn->next;
        if (now - conn->last_active > CONN_TIMEOUT_SEC) {
            LOG_INFO("Connection fd=%d timed out (%lds idle)", conn->fd,
                     (long)(now - conn->last_active));
            _close_conn(s, conn);
        }
        conn = next;
    }
}

/* ── 事件循环 ───────────────────────────────────────────────────────── */

int cmd_server_run(cmd_server_t* s)
{
    if (!s || s->listener_count == 0) {
        LOG_ERROR("server_run: no listeners configured");
        return -1;
    }

    struct epoll_event events[MAX_EVENTS];
    s->running = 1;

    LOG_INFO("Command server started (%d listeners, epoll_fd=%d)",
             s->listener_count, s->epoll_fd);

    while (s->running) {
        int nfds = epoll_wait(s->epoll_fd, events, MAX_EVENTS, IDLE_CHECK_MS);

        if (nfds < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("epoll_wait failed: %s", strerror(errno));
            break;
        }

        for (int i = 0; i < nfds; i++) {
            /* 监听 fd: 新连接 */
            if (events[i].events & EPOLLIN) {
                int fd = events[i].data.fd;
                int is_listener = 0;
                for (int j = 0; j < s->listener_count; j++) {
                    if (s->listener_fds[j] == fd) { is_listener = 1; break; }
                }

                if (is_listener) {
                    _accept_conn(s, fd);
                } else {
                    cmd_conn_t* conn = (cmd_conn_t*)events[i].data.ptr;
                    _handle_read(s, conn);
                }
            }

            /* 连接 fd: 可写 */
            if (events[i].events & EPOLLOUT) {
                cmd_conn_t* conn = (cmd_conn_t*)events[i].data.ptr;
                _handle_write(s, conn);
            }

            /* 连接 fd: 错误/挂断 */
            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                cmd_conn_t* conn = (cmd_conn_t*)events[i].data.ptr;
                LOG_DEBUG("Socket error/hangup on fd=%d", conn->fd);
                _close_conn(s, conn);
            }
        }

        /* 空闲连接检查 */
        _check_idle(s);
    }

    LOG_INFO("Command server stopped");
    return 0;
}

void cmd_server_stop(cmd_server_t* s)
{
    if (s) s->running = 0;
}
```

- [ ] **Step 5: 编写 cmd_server.c（第四部分 — 公开连接操作，conn 初始化需设置 epoll_fd = s->epoll_fd）**

```c
/* ── 连接操作 ───────────────────────────────────────────────────────── */

int cmd_conn_send(cmd_conn_t* conn, const cmd_frame_t* frame)
{
    if (!conn || !frame) return -1;

    /* 获取 epoll fd 需要从 conn 追溯服务器... 
       这里我们采用简化方案: 在连接上存储 epoll_fd 以便 EPOLLOUT 注册。
       修改 cmd_conn_t 增加 epoll_fd 字段。 */

    /* 组包 */
    size_t total = CMD_FRAME_HEADER_SIZE + 1 + frame->len;  /* header + CRC + payload */
    uint8_t* data = (uint8_t*)malloc(total);
    if (!data) return -1;

    size_t out_len = 0;
    if (cmd_protocol_pack(frame, data, total, &out_len) != 0) {
        free(data);
        return -1;
    }

    /* 构造发送节点 */
    struct tx_node* node = (struct tx_node*)malloc(sizeof(struct tx_node));
    if (!node) { free(data); return -1; }

    node->data = data;
    node->len  = out_len;
    node->sent = 0;
    node->next = NULL;

    hw_mutex_lock(&conn->tx_lock);

    int was_empty = (conn->tx_head == NULL);

    if (conn->tx_tail) {
        conn->tx_tail->next = node;
    } else {
        conn->tx_head = node;
    }
    conn->tx_tail = node;

    /* 如果之前队列为空，需要注册 EPOLLOUT */
    if (was_empty && conn->epoll_fd >= 0) {
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events   = EPOLLIN | EPOLLOUT | EPOLLET;
        ev.data.ptr = conn;
        epoll_ctl(conn->epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
    }

    hw_mutex_unlock(&conn->tx_lock);
    return 0;
}

void cmd_conn_close(cmd_server_t* s, cmd_conn_t* conn)
{
    if (s && conn) _close_conn(s, conn);
}

void* cmd_conn_get_ctx(cmd_conn_t* conn)
{
    return conn ? conn->ctx : NULL;
}

void cmd_conn_set_ctx(cmd_conn_t* conn, void* ctx)
{
    if (conn) conn->ctx = ctx;
}
```

- [ ] **Step 6: 验证编译**

```bash
gcc -std=c11 -c -I modules/cmd/include -I modules/log/include -I hw/include \
    -o /tmp/cmd_server.o modules/cmd/src/cmd_server.c
```

Expected: 成功，无错误。

- [ ] **Step 8: 提交**

```bash
git add modules/cmd/include/cmd/cmd_server.h modules/cmd/src/cmd_server.c
git commit -m "feat(cmd): add server layer — epoll event loop and connection management

- cmd_server_t: epoll-based event loop with accept/read/write/timeout
- cmd_conn_t: per-connection rx_buf, tx_queue (lock-protected), idle tracking
- Edge-triggered epoll for high performance
- 60s idle timeout auto-kicks stale connections
- cmd_conn_send: thread-safe frame queuing with EPOLLOUT registration

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 6: 订阅管理 (cmd_subscription)

**Files:**
- Create: `modules/cmd/include/cmd/cmd_subscription.h`
- Create: `modules/cmd/src/cmd_subscription.c`

**Interfaces:**
- Consumes: `cmd_conn_t`（通过 `cmd_server.h` 的前向声明），`cmd_frame_t`
- Produces: `cmd_subscription_create/destroy()`, `cmd_subscription_add/remove()`, `cmd_subscription_push()`

**目的:** 管理数据流订阅关系，支持按 data_id 添加/移除订阅者，以及向所有订阅者推送数据。

- [ ] **Step 1: 编写 cmd_subscription.h**

```c
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
 * @param data_id    数据流 ID
 * @param value      数据值（已为大端序）
 * @param value_len  数据值长度（字节）
 * @param cmd        命令大类（用于组帧，通常为 CMD_SENSOR）
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
```

- [ ] **Step 2: 编写 cmd_subscription.c**

```c
/**
 * cmd_subscription.c -- 数据流订阅管理实现
 *
 * 内部用链表存储 (data_id + 订阅者列表) 的二维结构。
 * 线程安全：所有公开 API 内部持有互斥锁。
 */

#define _GNU_SOURCE
#include "cmd/cmd_subscription.h"
#include "cmd/cmd_server.h"
#include "cmd/cmd_protocol.h"
#include "log/log.h"
#include "hw/hw_mutex.h"

#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>  /* htons */

/* ── 订阅者节点 ─────────────────────────────────────────────────────── */

typedef struct sub_node {
    cmd_conn_t*       conn;
    uint32_t          interval_ms;
    struct sub_node*  next;
} sub_node_t;

/* ── 数据流条目 ─────────────────────────────────────────────────────── */

typedef struct data_stream {
    uint16_t          data_id;
    sub_node_t*       subs_head;
    struct data_stream* next;
} data_stream_t;

/* ── 管理器 ─────────────────────────────────────────────────────────── */

typedef struct cmd_subscription_mgr {
    data_stream_t*    streams_head;
    hw_mutex_t        lock;
} cmd_subscription_mgr_t;

/* ── 内部: 查找或创建 data_stream ────────────────────────────────────── */

/**
 * 内部：在 streams 链表中查找 data_id，找不到返回 NULL。
 * 调用者必须持有锁。
 */
static data_stream_t* _find_stream(cmd_subscription_mgr_t* mgr, uint16_t data_id)
{
    data_stream_t* ds = mgr->streams_head;
    while (ds) {
        if (ds->data_id == data_id) return ds;
        ds = ds->next;
    }
    return NULL;
}

/**
 * 内部：查找或创建 data_stream。返回 data_stream 指针。
 * 调用者必须持有锁。
 */
static data_stream_t* _ensure_stream(cmd_subscription_mgr_t* mgr, uint16_t data_id)
{
    data_stream_t* ds = _find_stream(mgr, data_id);
    if (ds) return ds;

    ds = (data_stream_t*)calloc(1, sizeof(data_stream_t));
    if (!ds) return NULL;

    ds->data_id = data_id;
    ds->next = mgr->streams_head;
    mgr->streams_head = ds;
    return ds;
}

/* ── 公开 API ───────────────────────────────────────────────────────── */

cmd_subscription_mgr_t* cmd_subscription_create(void)
{
    cmd_subscription_mgr_t* mgr = (cmd_subscription_mgr_t*)calloc(1, sizeof(*mgr));
    if (mgr) hw_mutex_init(&mgr->lock);
    return mgr;
}

void cmd_subscription_destroy(cmd_subscription_mgr_t* mgr)
{
    if (!mgr) return;

    hw_mutex_lock(&mgr->lock);
    data_stream_t* ds = mgr->streams_head;
    while (ds) {
        data_stream_t* ds_next = ds->next;
        sub_node_t* sn = ds->subs_head;
        while (sn) {
            sub_node_t* sn_next = sn->next;
            free(sn);
            sn = sn_next;
        }
        free(ds);
        ds = ds_next;
    }
    hw_mutex_unlock(&mgr->lock);
    hw_mutex_destroy(&mgr->lock);
    free(mgr);
}

int cmd_subscription_add(cmd_subscription_mgr_t* mgr, uint16_t data_id,
                         uint32_t interval_ms, cmd_conn_t* conn)
{
    if (!mgr || !conn) return -1;

    hw_mutex_lock(&mgr->lock);

    /* 检查是否已有订阅（同一 conn + data_id） */
    data_stream_t* ds = _find_stream(mgr, data_id);
    if (ds) {
        sub_node_t* sn = ds->subs_head;
        while (sn) {
            if (sn->conn == conn) {
                sn->interval_ms = interval_ms;  /* 更新间隔 */
                hw_mutex_unlock(&mgr->lock);
                LOG_DEBUG("Subscription updated: data_id=0x%04X fd=%d interval=%ums",
                          data_id, conn->fd, interval_ms);
                return 0;
            }
            sn = sn->next;
        }
    }

    /* 新建订阅 */
    ds = _ensure_stream(mgr, data_id);
    if (!ds) {
        hw_mutex_unlock(&mgr->lock);
        return -1;
    }

    sub_node_t* sn = (sub_node_t*)calloc(1, sizeof(sub_node_t));
    if (!sn) {
        hw_mutex_unlock(&mgr->lock);
        return -1;
    }
    sn->conn = conn;
    sn->interval_ms = interval_ms;
    sn->next = ds->subs_head;
    ds->subs_head = sn;

    hw_mutex_unlock(&mgr->lock);
    LOG_DEBUG("Subscription added: data_id=0x%04X fd=%d interval=%ums",
              data_id, conn->fd, interval_ms);
    return 0;
}

int cmd_subscription_remove(cmd_subscription_mgr_t* mgr, uint16_t data_id,
                            cmd_conn_t* conn)
{
    if (!mgr || !conn) return -1;

    hw_mutex_lock(&mgr->lock);

    data_stream_t* ds = _find_stream(mgr, data_id);
    if (!ds) { hw_mutex_unlock(&mgr->lock); return -1; }

    sub_node_t* prev = NULL;
    sub_node_t* sn = ds->subs_head;
    while (sn) {
        if (sn->conn == conn) {
            if (prev) prev->next = sn->next;
            else ds->subs_head = sn->next;
            free(sn);
            hw_mutex_unlock(&mgr->lock);
            LOG_DEBUG("Subscription removed: data_id=0x%04X fd=%d", data_id, conn->fd);
            return 0;
        }
        prev = sn;
        sn = sn->next;
    }

    hw_mutex_unlock(&mgr->lock);
    return -1;
}

int cmd_subscription_push(cmd_subscription_mgr_t* mgr, uint8_t cmd,
                          uint16_t data_id, const uint8_t* value,
                          size_t value_len)
{
    if (!mgr || !value || value_len == 0) return 0;

    hw_mutex_lock(&mgr->lock);

    data_stream_t* ds = _find_stream(mgr, data_id);
    if (!ds || !ds->subs_head) {
        hw_mutex_unlock(&mgr->lock);
        return 0;
    }

    /* 组推送帧: PAYLOAD = [data_id 2B BE | value ...] */
    size_t pld_len = 2 + value_len;
    uint8_t* pld = (uint8_t*)malloc(pld_len);
    if (!pld) { hw_mutex_unlock(&mgr->lock); return 0; }

    uint16_t id_be = htons(data_id);
    memcpy(pld, &id_be, 2);
    memcpy(pld + 2, value, value_len);

    cmd_frame_t frame;
    frame.cmd     = cmd;
    frame.sub     = cmd_frame_sub_rsp(CMD_SUB_SUBSCRIBE);  /* 0x83 */
    frame.len     = (uint16_t)pld_len;
    frame.payload = pld;

    /* 遍历订阅者发送 */
    int count = 0;
    sub_node_t* sn = ds->subs_head;
    while (sn) {
        if (cmd_conn_send(sn->conn, &frame) == 0) count++;
        sn = sn->next;
    }

    free(pld);
    hw_mutex_unlock(&mgr->lock);
    return count;
}

void cmd_subscription_remove_all(cmd_subscription_mgr_t* mgr, cmd_conn_t* conn)
{
    if (!mgr || !conn) return;

    hw_mutex_lock(&mgr->lock);

    data_stream_t* ds = mgr->streams_head;
    while (ds) {
        sub_node_t* prev = NULL;
        sub_node_t* sn = ds->subs_head;
        while (sn) {
            if (sn->conn == conn) {
                sub_node_t* to_free = sn;
                if (prev) prev->next = sn->next;
                else ds->subs_head = sn->next;
                sn = sn->next;
                free(to_free);
            } else {
                prev = sn;
                sn = sn->next;
            }
        }
        ds = ds->next;
    }

    hw_mutex_unlock(&mgr->lock);
}
```

- [ ] **Step 3: 验证编译**

```bash
gcc -std=c11 -c -I modules/cmd/include -I modules/log/include -I hw/include \
    -o /tmp/cmd_subscription.o modules/cmd/src/cmd_subscription.c
```

Expected: 成功，无错误。

- [ ] **Step 4: 提交**

```bash
git add modules/cmd/include/cmd/cmd_subscription.h modules/cmd/src/cmd_subscription.c
git commit -m "feat(cmd): add subscription manager for data stream push

- cmd_subscription_add/remove: manage per-data_id subscriber lists
- cmd_subscription_push: build push frame and send to all subscribers
- cmd_subscription_remove_all: clean up all subscriptions for a connection
- Thread-safe via hw_mutex

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 7: 调度层 (cmd_dispatcher)

**Files:**
- Create: `modules/cmd/include/cmd/cmd_dispatcher.h`
- Create: `modules/cmd/src/cmd_dispatcher.c`

**Interfaces:**
- Consumes: `cmd_frame_t`, `cmd_conn_t`, `cmd_subscription_mgr_t`
- Produces: `cmd_dispatcher_create/destroy()`, `cmd_dispatcher_register()`, `cmd_dispatcher_dispatch()`

**目的:** CMD 大类 → handler 路由，自动处理未知命令错误响应。

- [ ] **Step 1: 编写 cmd_dispatcher.h**

```c
#ifndef CMD_DISPATCHER_H
#define CMD_DISPATCHER_H

#include "cmd/cmd_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 前向声明 */
typedef struct cmd_conn     cmd_conn_t;
typedef struct cmd_dispatcher cmd_dispatcher_t;
typedef struct cmd_subscription_mgr cmd_subscription_mgr_t;

/**
 * 命令处理函数类型。
 *
 * @param req   请求帧（只读）
 * @param conn  来源连接
 * @param ctx   用户上下文（由 dispatcher_create 时传入）
 */
typedef void (*cmd_handler_fn)(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx);

/**
 * 创建命令调度器。
 *
 * @param sub_mgr  订阅管理器（供 handler 使用），可为 NULL
 * @param ctx      用户上下文，传递给每个 handler
 * @return         成功返回实例指针，失败返回 NULL
 */
cmd_dispatcher_t* cmd_dispatcher_create(cmd_subscription_mgr_t* sub_mgr, void* ctx);

/**
 * 销毁调度器。
 *
 * @param d  调度器实例
 */
void cmd_dispatcher_destroy(cmd_dispatcher_t* d);

/**
 * 注册命令处理函数。
 *
 * @param d       调度器实例
 * @param cmd     命令大类（0x01 ~ 0xFE）
 * @param handler 处理函数
 * @return        0 成功，-1 已注册
 */
int cmd_dispatcher_register(cmd_dispatcher_t* d, uint8_t cmd, cmd_handler_fn handler);

/**
 * 分发请求帧到对应 handler。
 *
 * 若 CMD 未注册 handler，自动通过 cmd_conn_send 返回错误响应（CMD_ERR_UNKNOWN_CMD）。
 *
 * @param d     调度器实例
 * @param req   请求帧
 * @param conn  来源连接
 */
void cmd_dispatcher_dispatch(cmd_dispatcher_t* d, const cmd_frame_t* req,
                             cmd_conn_t* conn);

/**
 * 获取调度器的用户上下文。
 *
 * @param d  调度器实例
 * @return   用户上下文指针
 */
void* cmd_dispatcher_get_ctx(cmd_dispatcher_t* d);

/**
 * 获取调度器的订阅管理器。
 *
 * @param d  调度器实例
 * @return   订阅管理器指针（可为 NULL）
 */
cmd_subscription_mgr_t* cmd_dispatcher_get_sub_mgr(cmd_dispatcher_t* d);

#ifdef __cplusplus
}
#endif

#endif /* CMD_DISPATCHER_H */
```

- [ ] **Step 2: 编写 cmd_dispatcher.c**

```c
/**
 * cmd_dispatcher.c -- 命令调度实现
 *
 * 内部持有 CMD → handler 的路由表，将解析后的请求帧路由到对应的命令处理器。
 * 自动处理未知命令和未知子命令的错误响应。
 */

#define _GNU_SOURCE
#include "cmd/cmd_dispatcher.h"
#include "cmd/cmd_server.h"
#include "cmd/cmd_subscription.h"
#include "cmd/cmd_protocol.h"
#include "log/log.h"

#include <stdlib.h>
#include <string.h>

#define MAX_HANDLERS 256  /* CMD 是 1 字节 */

typedef struct cmd_dispatcher {
    cmd_handler_fn          handlers[MAX_HANDLERS];
    cmd_subscription_mgr_t* sub_mgr;
    void*                   ctx;
} cmd_dispatcher_t;

/* ── 发送错误响应的内部辅助 ─────────────────────────────────────────── */

/**
 * 内部：向 conn 发送一个错误响应帧。
 *
 * @param conn       目标连接
 * @param req_cmd    原始请求的 CMD
 * @param req_sub    原始请求的 SUB
 * @param error_code 错误码
 */
static void _send_error(cmd_conn_t* conn, uint8_t req_cmd, uint8_t req_sub,
                        uint8_t error_code)
{
    uint8_t err_pld = error_code;
    cmd_frame_t rsp;
    rsp.cmd     = req_cmd;
    rsp.sub     = cmd_frame_sub_rsp(req_sub);
    rsp.len     = 1;
    rsp.payload = &err_pld;

    cmd_conn_send(conn, &rsp);
}

/* ── 公开 API ───────────────────────────────────────────────────────── */

cmd_dispatcher_t* cmd_dispatcher_create(cmd_subscription_mgr_t* sub_mgr, void* ctx)
{
    cmd_dispatcher_t* d = (cmd_dispatcher_t*)calloc(1, sizeof(cmd_dispatcher_t));
    if (d) {
        d->sub_mgr = sub_mgr;
        d->ctx     = ctx;
    }
    return d;
}

void cmd_dispatcher_destroy(cmd_dispatcher_t* d)
{
    free(d);
}

int cmd_dispatcher_register(cmd_dispatcher_t* d, uint8_t cmd, cmd_handler_fn handler)
{
    if (!d || !handler) return -1;
    if (d->handlers[cmd]) return -1;  /* 已注册 */

    d->handlers[cmd] = handler;
    LOG_DEBUG("Dispatcher: registered handler for CMD=0x%02X", cmd);
    return 0;
}

void cmd_dispatcher_dispatch(cmd_dispatcher_t* d, const cmd_frame_t* req,
                             cmd_conn_t* conn)
{
    if (!d || !req || !conn) return;

    uint8_t cmd = req->cmd;
    cmd_handler_fn handler = d->handlers[cmd];

    if (!handler) {
        LOG_DEBUG("Dispatcher: unknown CMD=0x%02X from fd=%d", cmd, conn->fd);
        _send_error(conn, cmd, req->sub, CMD_ERR_UNKNOWN_CMD);
        return;
    }

    handler(req, conn, d->ctx);
}

void* cmd_dispatcher_get_ctx(cmd_dispatcher_t* d)
{
    return d ? d->ctx : NULL;
}

cmd_subscription_mgr_t* cmd_dispatcher_get_sub_mgr(cmd_dispatcher_t* d)
{
    return d ? d->sub_mgr : NULL;
}
```

- [ ] **Step 3: 验证编译**

```bash
gcc -std=c11 -c -I modules/cmd/include -I modules/log/include -I hw/include \
    -o /tmp/cmd_dispatcher.o modules/cmd/src/cmd_dispatcher.c
```

Expected: 成功，无错误。

- [ ] **Step 4: 提交**

```bash
git add modules/cmd/include/cmd/cmd_dispatcher.h modules/cmd/src/cmd_dispatcher.c
git commit -m "feat(cmd): add dispatcher — CMD to handler routing

- 256-entry handler table indexed by CMD byte
- Auto-replies CMD_ERR_UNKNOWN_CMD for unregistered commands
- Passes user context and subscription manager to handlers

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 8: LED 命令处理器 (cmd_handler_led)

**Files:**
- Create: `modules/cmd/src/cmd_handler_led.c`

**Interfaces:**
- Consumes: `cmd_dispatcher_register()`（通过 handler 签名），`cmd_conn_send()`，`dev_led.h`（hw 层）
- Produces: `cmd_handler_led()` 函数，由 Task 10 注册到 dispatcher

**目的:** 处理 CMD=0x01 的 LED 读写命令。

- [ ] **Step 1: 编写 cmd_handler_led.c**

```c
/**
 * cmd_handler_led.c -- LED 命令处理器
 *
 * 处理 CMD=0x01:
 *   SUB=0x01 写: PAYLOAD=[led_id 1B, state 1B]  → 调用 led_on/led_off
 *   SUB=0x02 读: PAYLOAD=[led_id 1B]            → 读取 brightness，返回 [led_id, state]
 *
 * 每个处理器通过 void* ctx 获取硬件句柄。
 * ctx 指向 cmd_handler_ctx_t 结构（定义在 cmd_handler_ctx.h）。
 */

#define _GNU_SOURCE
#include "cmd/cmd_dispatcher.h"
#include "cmd/cmd_server.h"
#include "cmd/cmd_protocol.h"
#include "cmd/cmd_subscription.h"
#include "cmd/cmd_handler_ctx.h"
#include "hw/dev/dev_led.h"
#include "log/log.h"

#include <string.h>

void cmd_handler_led(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx)
{
    cmd_handler_ctx_t* hctx = (cmd_handler_ctx_t*)ctx;
    if (!hctx || !hctx->led) {
        /* 无 LED 硬件，返回错误 */
        uint8_t err = CMD_ERR_HARDWARE;
        cmd_frame_t rsp = { .cmd = CMD_LED, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 1, .payload = &err };
        cmd_conn_send(conn, &rsp);
        return;
    }

    uint8_t op = cmd_frame_sub_req(req->sub);

    if (op == CMD_SUB_WRITE) {
        /* 写 LED: PAYLOAD=[led_id, state] */
        if (req->len < 2) {
            uint8_t err = CMD_ERR_PARAM;
            cmd_frame_t rsp = { .cmd = CMD_LED, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        uint8_t led_id = req->payload[0];
        uint8_t state  = req->payload[1];
        hw_err_t ret;

        if (state) {
            ret = led_on(hctx->led);
        } else {
            ret = led_off(hctx->led);
        }

        uint8_t rsp_pld[3];
        rsp_pld[0] = (ret == HW_OK) ? CMD_ERR_OK : CMD_ERR_HARDWARE;
        rsp_pld[1] = led_id;
        rsp_pld[2] = state;

        cmd_frame_t rsp = { .cmd = CMD_LED, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 3, .payload = rsp_pld };
        cmd_conn_send(conn, &rsp);

        LOG_DEBUG("LED write: id=%d state=%d result=%d", led_id, state, ret);

    } else if (op == CMD_SUB_READ) {
        /* 读 LED: PAYLOAD=[led_id] */
        if (req->len < 1) {
            uint8_t err = CMD_ERR_PARAM;
            cmd_frame_t rsp = { .cmd = CMD_LED, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        uint8_t led_id = req->payload[0];
        int brightness = 0;
        hw_err_t ret = led_get_brightness(hctx->led, &brightness);

        uint8_t rsp_pld[3];
        rsp_pld[0] = (ret == HW_OK) ? CMD_ERR_OK : CMD_ERR_HARDWARE;
        rsp_pld[1] = led_id;
        rsp_pld[2] = (brightness > 0) ? 1 : 0;

        cmd_frame_t rsp = { .cmd = CMD_LED, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 3, .payload = rsp_pld };
        cmd_conn_send(conn, &rsp);

        LOG_DEBUG("LED read: id=%d brightness=%d", led_id, brightness);

    } else {
        /* 未知子命令 */
        uint8_t err = CMD_ERR_UNKNOWN_SUB;
        cmd_frame_t rsp = { .cmd = CMD_LED, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 1, .payload = &err };
        cmd_conn_send(conn, &rsp);
    }
}
```

- [ ] **Step 2: 同步创建 handler 上下文头文件**

```bash
cat > modules/cmd/include/cmd/cmd_handler_ctx.h << 'EOF'
#ifndef CMD_HANDLER_CTX_H
#define CMD_HANDLER_CTX_H

#ifdef __cplusplus
extern "C" {
#endif

/* 前向声明 hw 类型 */
typedef struct led_ctx   led_t;
typedef struct sht30_ctx sht30_t;

/* 前向声明 cmd_subscription_mgr_t */
typedef struct cmd_subscription_mgr cmd_subscription_mgr_t;

/**
 * handler 共享上下文，通过 dispatcher 的 void* ctx 传递。
 *
 * 所有 handler 通过此结构访问硬件句柄和订阅管理器。
 * 未初始化的句柄设为 NULL，handler 内检查 NULL 决定是否可用。
 */
typedef struct {
    led_t*                    led;      /* LED 句柄，可为 NULL       */
    sht30_t*                  sht30;    /* SHT30 传感器句柄，可为 NULL */
    cmd_subscription_mgr_t*   sub_mgr;  /* 订阅管理器，可为 NULL     */
} cmd_handler_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* CMD_HANDLER_CTX_H */
EOF
```

- [ ] **Step 3: 验证编译**

```bash
gcc -std=c11 -c -I modules/cmd/include -I modules/log/include -I hw/include \
    -o /tmp/cmd_handler_led.o modules/cmd/src/cmd_handler_led.c
```

Expected: 成功，无错误。

- [ ] **Step 4: 提交**

```bash
git add modules/cmd/src/cmd_handler_led.c modules/cmd/include/cmd/cmd_handler_ctx.h
git commit -m "feat(cmd): add LED command handler (CMD=0x01)

- Handle SUB=0x01 (write): control LED on/off via led_on/led_off
- Handle SUB=0x02 (read): read brightness via led_get_brightness
- Return error codes: HW_ERR_PARAM, HW_ERR_HARDWARE, HW_ERR_UNKNOWN_SUB
- Add cmd_handler_ctx_t shared context for hardware handles

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 9: 传感器命令处理器 (cmd_handler_sensor)

**Files:**
- Create: `modules/cmd/src/cmd_handler_sensor.c`

**Interfaces:**
- Consumes: `cmd_dispatcher_*`, `cmd_conn_send()`, `cmd_subscription_*`, `dev_sht30.h`
- Produces: `cmd_handler_sensor()` 函数

**目的:** 处理 CMD=0x02 的传感器读取、订阅和取消订阅命令。

- [ ] **Step 1: 编写 cmd_handler_sensor.c**

```c
/**
 * cmd_handler_sensor.c -- 传感器命令处理器
 *
 * 处理 CMD=0x02:
 *   SUB=0x02 读: PAYLOAD=空 → 返回当前温度(float BE)
 *   SUB=0x03 订阅: PAYLOAD=[data_id 2B BE, interval_ms 2B BE]
 *   SUB=0x04 取消订阅: PAYLOAD=[data_id 2B BE]
 *
 * 订阅/取消操作通过 dispatcher 的 sub_mgr 完成。
 */

#define _GNU_SOURCE
#include "cmd/cmd_dispatcher.h"
#include "cmd/cmd_server.h"
#include "cmd/cmd_protocol.h"
#include "cmd/cmd_subscription.h"
#include "cmd/cmd_handler_ctx.h"
#include "hw/dev/dev_sht30.h"
#include "log/log.h"

#include <string.h>
#include <arpa/inet.h>  /* ntohs, htonl */

void cmd_handler_sensor(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx)
{
    cmd_handler_ctx_t* hctx = (cmd_handler_ctx_t*)ctx;
    cmd_subscription_mgr_t* sub_mgr = hctx ? hctx->sub_mgr : NULL;

    uint8_t op = cmd_frame_sub_req(req->sub);

    if (op == CMD_SUB_READ) {
        /* 读一次传感器 */
        if (!hctx || !hctx->sht30) {
            uint8_t err = CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_SENSOR, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        float temp_c = 0.0f;
        float humidity = 0.0f;
        hw_err_t ret_t = sht30_read_temperature(hctx->sht30, &temp_c);
        hw_err_t ret_h = sht30_read_humidity(hctx->sht30, &humidity);

        if (ret_t != HW_OK && ret_h != HW_OK) {
            uint8_t err = CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_SENSOR, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        /* PAYLOAD = [err 1B, temp float BE 4B, humidity float BE 4B] */
        uint8_t pld[9];
        pld[0] = CMD_ERR_OK;

        uint32_t tmp;
        memcpy(&tmp, &temp_c, 4);
        tmp = htonl(tmp);
        memcpy(pld + 1, &tmp, 4);

        memcpy(&tmp, &humidity, 4);
        tmp = htonl(tmp);
        memcpy(pld + 5, &tmp, 4);

        cmd_frame_t rsp = { .cmd = CMD_SENSOR, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 9, .payload = pld };
        cmd_conn_send(conn, &rsp);

        LOG_DEBUG("Sensor read: temp=%.1f°C humidity=%.1f%%", temp_c, humidity);

    } else if (op == CMD_SUB_SUBSCRIBE) {
        /* 订阅数据流: PAYLOAD=[data_id 2B BE, interval_ms 2B BE] */
        if (req->len < 4 || !sub_mgr) {
            uint8_t err = req->len < 4 ? CMD_ERR_PARAM : CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_SENSOR, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        uint16_t data_id_be, interval_be;
        memcpy(&data_id_be, req->payload, 2);
        memcpy(&interval_be, req->payload + 2, 2);
        uint16_t data_id  = ntohs(data_id_be);
        uint32_t interval = ntohs(interval_be);

        int rc = cmd_subscription_add(sub_mgr, data_id, interval, conn);

        uint8_t rsp_pld[5];
        rsp_pld[0] = (rc == 0) ? CMD_ERR_OK : CMD_ERR_PARAM;
        memcpy(rsp_pld + 1, &data_id_be, 2);
        memcpy(rsp_pld + 3, &interval_be, 2);

        cmd_frame_t rsp = { .cmd = CMD_SENSOR, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 5, .payload = rsp_pld };
        cmd_conn_send(conn, &rsp);

        LOG_DEBUG("Sensor subscribe: data_id=0x%04X interval=%ums fd=%d rc=%d",
                  data_id, interval, conn->fd, rc);

    } else if (op == CMD_SUB_UNSUBSCRIBE) {
        /* 取消订阅: PAYLOAD=[data_id 2B BE] */
        if (req->len < 2 || !sub_mgr) {
            uint8_t err = req->len < 2 ? CMD_ERR_PARAM : CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_SENSOR, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        uint16_t data_id_be;
        memcpy(&data_id_be, req->payload, 2);
        uint16_t data_id = ntohs(data_id_be);

        int rc = cmd_subscription_remove(sub_mgr, data_id, conn);

        uint8_t rsp_pld[3];
        rsp_pld[0] = (rc == 0) ? CMD_ERR_OK : CMD_ERR_PARAM;
        memcpy(rsp_pld + 1, &data_id_be, 2);

        cmd_frame_t rsp = { .cmd = CMD_SENSOR, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 3, .payload = rsp_pld };
        cmd_conn_send(conn, &rsp);

        LOG_DEBUG("Sensor unsubscribe: data_id=0x%04X fd=%d rc=%d",
                  data_id, conn->fd, rc);

    } else {
        uint8_t err = CMD_ERR_UNKNOWN_SUB;
        cmd_frame_t rsp = { .cmd = CMD_SENSOR, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 1, .payload = &err };
        cmd_conn_send(conn, &rsp);
    }
}
```

- [ ] **Step 2: 验证编译**

```bash
gcc -std=c11 -c -I modules/cmd/include -I modules/log/include -I hw/include \
    -o /tmp/cmd_handler_sensor.o modules/cmd/src/cmd_handler_sensor.c
```

Expected: 成功，无错误。

- [ ] **Step 3: 提交**

```bash
git add modules/cmd/src/cmd_handler_sensor.c
git commit -m "feat(cmd): add sensor command handler (CMD=0x02)

- Handle SUB=0x02 (read once): return temperature and humidity as floats
- Handle SUB=0x03 (subscribe): register data stream subscription
- Handle SUB=0x04 (unsubscribe): remove subscription
- All multi-byte fields use big-endian (network byte order)

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 10: 系统命令处理器 (cmd_handler_system)

**Files:**
- Create: `modules/cmd/src/cmd_handler_system.c`

**Interfaces:**
- Consumes: `log.h`（`log_set_level`），`cmd_dispatcher.h`，`cmd_server.h`
- Produces: `cmd_handler_system()` 函数

**目的:** 处理 CMD=0x03 的系统查询和日志等级设置。

- [ ] **Step 1: 编写 cmd_handler_system.c**

```c
/**
 * cmd_handler_system.c -- 系统命令处理器
 *
 * 处理 CMD=0x03:
 *   SUB=0x02 查询系统信息: 返回版本字符串
 *   SUB=0x03 设置日志等级: PAYLOAD=[level 1B] 0=DEBUG 1=INFO 2=WARN 3=ERROR
 */

#define _GNU_SOURCE
#include "cmd/cmd_dispatcher.h"
#include "cmd/cmd_server.h"
#include "cmd/cmd_protocol.h"
#include "log/log.h"

#include <string.h>

#define APP_VERSION  "1.0.0"

void cmd_handler_system(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx)
{
    (void)ctx;  /* system handler 不需要 ctx */

    uint8_t op = cmd_frame_sub_req(req->sub);

    if (op == CMD_SUB_READ) {
        /* 查询系统信息: 返回版本号 */
        const char* ver = APP_VERSION;
        size_t ver_len = strlen(ver);

        /* PAYLOAD = [err 1B, version_str N B] */
        uint8_t* pld = (uint8_t*)malloc(1 + ver_len);
        if (!pld) return;

        pld[0] = CMD_ERR_OK;
        memcpy(pld + 1, ver, ver_len);

        cmd_frame_t rsp;
        rsp.cmd     = CMD_SYSTEM;
        rsp.sub     = cmd_frame_sub_rsp(req->sub);
        rsp.len     = (uint16_t)(1 + ver_len);
        rsp.payload = pld;

        cmd_conn_send(conn, &rsp);
        free(pld);

        LOG_DEBUG("System info requested by fd=%d", conn->fd);

    } else if (op == CMD_SUB_WRITE) {
        /* 设置日志等级: PAYLOAD=[level 1B] */
        if (req->len < 1) {
            uint8_t err = CMD_ERR_PARAM;
            cmd_frame_t rsp = { .cmd = CMD_SYSTEM, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        uint8_t level = req->payload[0];
        if (level > LOG_ERROR) {
            uint8_t err = CMD_ERR_PARAM;
            cmd_frame_t rsp = { .cmd = CMD_SYSTEM, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        log_set_level((log_level_t)level);

        uint8_t rsp_pld[2];
        rsp_pld[0] = CMD_ERR_OK;
        rsp_pld[1] = level;

        cmd_frame_t rsp = { .cmd = CMD_SYSTEM, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 2, .payload = rsp_pld };
        cmd_conn_send(conn, &rsp);

        LOG_INFO("Log level changed to %d by fd=%d", level, conn->fd);

    } else {
        uint8_t err = CMD_ERR_UNKNOWN_SUB;
        cmd_frame_t rsp = { .cmd = CMD_SYSTEM, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 1, .payload = &err };
        cmd_conn_send(conn, &rsp);
    }
}
```

- [ ] **Step 2: 验证编译**

```bash
gcc -std=c11 -c -I modules/cmd/include -I modules/log/include -I hw/include \
    -o /tmp/cmd_handler_system.o modules/cmd/src/cmd_handler_system.c
```

Expected: 成功，无错误。

- [ ] **Step 3: 提交**

```bash
git add modules/cmd/src/cmd_handler_system.c
git commit -m "feat(cmd): add system command handler (CMD=0x03)

- Handle SUB=0x02 (read): return app version string
- Handle SUB=0x03 (write): set log level at runtime via log_set_level

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 11: CMake 构建集成

**Files:**
- Create: `modules/cmd/CMakeLists.txt`
- Modify: `modules/CMakeLists.txt` — 添加 `add_subdirectory(cmd)`
- Modify: `CMakeLists.txt` — 将 cmd 库链接到 project_app

**Interfaces:**
- Consumes: 所有 cmd 源文件和头文件
- Produces: `libcmd.a` 静态库

**目的:** 将 cmd 模块编译为 `libcmd.a`，并链接到 `project_app`。

- [ ] **Step 1: 编写 modules/cmd/CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)

set(CMD_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_transport.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_dispatcher.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_subscription.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_handler_led.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_handler_sensor.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_handler_system.c
)

set(CMD_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cmd/cmd_frame.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cmd/cmd_protocol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cmd/cmd_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cmd/cmd_server.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cmd/cmd_dispatcher.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cmd/cmd_subscription.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cmd/cmd_handler_ctx.h
)

# 静态库 libcmd.a
add_library(cmd STATIC ${CMD_SOURCES} ${CMD_HEADERS})
target_include_directories(cmd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cmd PUBLIC hw log pthread)

# 单元测试（仅 host 编译）
if(BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
    add_executable(test_cmd_protocol ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cmd_protocol.c)
    target_link_libraries(test_cmd_protocol cmd)
    add_test(NAME test_cmd_protocol COMMAND test_cmd_protocol)
endif()
```

- [ ] **Step 2: 修改 modules/CMakeLists.txt**

在 `add_subdirectory(log)` 后追加：

```cmake
add_subdirectory(cmd)
```

- [ ] **Step 3: 修改根 CMakeLists.txt**

在 `target_link_libraries(project_app hw log)` 中加入 `cmd`：

```cmake
target_link_libraries(project_app hw log cmd)
```

- [ ] **Step 4: 验证编译（交叉编译环境）**

```bash
cd build && cmake .. && make -j$(nproc)
```

Expected: `libcmd.a` 编译成功，`project_app` 链接成功。

- [ ] **Step 5: 在 host 上运行单元测试**

```bash
cd build && cmake -DBUILD_TESTS=ON .. && make test_cmd_protocol && ./modules/cmd/test_cmd_protocol
```

Expected: 8/8 测试通过。

- [ ] **Step 6: 提交**

```bash
git add modules/cmd/CMakeLists.txt modules/CMakeLists.txt CMakeLists.txt
git commit -m "build(cmd): add CMake integration for cmd module

- Build libcmd.a static library with all cmd sources
- Link cmd into project_app
- Gate test_cmd_protocol behind BUILD_TESTS (host-only)

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 12: 集成到 main.c — 传感器工作线程 + cmd_server 启动

**Files:**
- Modify: `user/main.c` — 替换主循环为 cmd_server + 传感器线程

**Interfaces:**
- Consumes: `cmd_server_*`, `cmd_dispatcher_*`, `cmd_subscription_*`, `cmd_handler_*`, `cmd_handler_ctx_t`
- Produces: 集成后的应用程序入口

**目的:** 将命令模块集成到 project_app，保留信号处理和优雅退出。

- [ ] **Step 1: 重写 main.c**

```c
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
```

- [ ] **Step 2: 验证交叉编译**

```bash
cd build && cmake .. && make -j$(nproc)
```

Expected: `project_app` 编译链接成功。

- [ ] **Step 3: 提交**

```bash
git add user/main.c
git commit -m "feat(cmd): integrate cmd module into main application

- Replace while(1) loop with cmd_server epoll event loop
- Move sensor polling to dedicated worker thread with subscription push
- Initialize LED, SHT30, dispatcher with 3 handlers, and dual listeners
- Graceful shutdown via existing signal handler + atexit chain

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 13: 端到端集成测试

**Files:**
- Create: `modules/cmd/tests/test_cmd_integration.c`（可选，Host 环境运行）

或者直接在开发板上手动验证。

**目的:** 验证 Unix Socket 和 TCP 两种传输方式的端到端命令流程。

- [ ] **Step 1: 创建测试脚本**

```bash
cat > /tmp/test_cmd.sh << 'TESTEOF'
#!/bin/bash
# 测试脚本 — 在 Orange Pi 5 Plus 上运行

echo "=== Test 1: LED On ==="
echo -ne '\xA5\x5A\x00\x01\x01\x01\x01' | socat - UNIX-CONNECT:/tmp/cmd.sock | xxd

echo "=== Test 2: LED Off ==="
echo -ne '\xA5\x5A\x00\x01\x01\x01\x00' | socat - UNIX-CONNECT:/tmp/cmd.sock | xxd

echo "=== Test 3: Read Temperature ==="
echo -ne '\xA5\x5A\x00\x00\x02\x02' | socat - UNIX-CONNECT:/tmp/cmd.sock | xxd

echo "=== Test 4: Read LED Status ==="
echo -ne '\xA5\x5A\x00\x01\x01\x02\x01' | socat - UNIX-CONNECT:/tmp/cmd.sock | xxd

echo "=== Test 5: System Info ==="
echo -ne '\xA5\x5A\x00\x00\x03\x02' | socat - UNIX-CONNECT:/tmp/cmd.sock | xxd

echo "=== Test 6: Subscribe Temperature (1s interval) ==="
echo -ne '\xA5\x5A\x00\x04\x02\x03\x00\x01\x00\x00\x03\xE8' | socat - UNIX-CONNECT:/tmp/cmd.sock | xxd

echo "=== All tests sent. Check /tmp/project.log for results ==="
TESTEOF
chmod +x /tmp/test_cmd.sh

# 也测试 TCP 连接
echo -ne '\xA5\x5A\x00\x00\x03\x02' | socat - TCP:localhost:9527 | xxd
```

- [ ] **Step 2: 在开发板上运行测试**

需要在开发板上先启动 project_app，然后运行测试脚本。

- [ ] **Step 3: 提交**

```bash
git add modules/cmd/tests/test_cmd.sh
git commit -m "test(cmd): add end-to-end integration test script

- Test LED on/off, temperature read, system info via Unix socket
- Test TCP connection on port 9527
- Uses socat for raw binary frame transmission

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## 任务依赖关系

```
Task 1 (frame.h) ─────────────────────────────────────────────────────┐
    │                                                                  │
Task 2 (protocol) ── Task 3 (protocol test) ──────────────────────┐   │
    │                                                               │   │
Task 4 (transport) ───────────────────────────────────────────┐    │   │
    │                                                          │    │   │
Task 5 (server) ──────────────────────────────────────────┐   │    │   │
    │                                                      │   │    │   │
Task 6 (subscription) ────────────────────────────────────┤   │    │   │
    │                                                      │   │    │   │
Task 7 (dispatcher) ──────────────────────────────────────┤   │    │   │
    │                                                      │   │    │   │
Task 8 (handler_led) ─────────────────────────────────────┤   │    │   │
    │                                                      │   │    │   │
Task 9 (handler_sensor) ──────────────────────────────────┤   │    │   │
    │                                                      │   │    │   │
Task 10 (handler_system) ─────────────────────────────────┤   │    │   │
    │                                                      │   │    │   │
Task 11 (CMake) ──────────────────────────────────────────┤   │    │   │
    │                                                      │   │    │   │
Task 12 (main.c integration) ─────────────────────────────┤   │    │   │
    │                                                      │   │    │   │
Task 13 (integration test) ───────────────────────────────┘   │    │   │
                                                               │    │   │
```

Task 1→2→3 是线性依赖。Task 4 可与 Task 2 并行。Task 5 依赖 2+4。Task 6 可与 Task 5 并行。Task 7 依赖 5+6。Task 8/9/10 依赖 7。Task 11 是构建集成，Task 12 是所有模块的大集成。
