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
#include <arpa/inet.h>  /* htonl, ntohl */

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
