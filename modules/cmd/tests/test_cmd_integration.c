/**
 * test_cmd_integration.c -- 命令模块集成测试（Host 环境）
 *
 * 在当前机器上启动 cmd_server + dispatcher + mock handler，
 * 通过 Unix Socket 发送二进制帧并验证响应。
 * 无需硬件依赖。
 */

#define _GNU_SOURCE
#include "cmd/cmd_server.h"
#include "cmd/cmd_dispatcher.h"
#include "cmd/cmd_transport.h"
#include "cmd/cmd_protocol.h"
#include "cmd/cmd_subscription.h"
#include "log/log.h"
#include "hw/hw_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST %s ... ", name); fflush(stdout); } while(0)
#define PASS()      do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg)   do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

/* ── 全局 ───────────────────────────────────────────────────────────── */

static cmd_server_t*          g_server = NULL;
static cmd_dispatcher_t*      g_dispatcher = NULL;
static cmd_subscription_mgr_t* g_sub_mgr = NULL;
static volatile int           g_server_running = 0;
static const char*            g_sock_path = "/tmp/test_cmd.sock";

/* ── Mock Handler ───────────────────────────────────────────────────── */

/**
 * Mock LED handler：回显收到的 payload 前加错误码 0x00。
 */
static void mock_handler_led(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx)
{
    (void)ctx;

    uint8_t rsp_pld[256];
    rsp_pld[0] = CMD_ERR_OK;
    if (req->len > 0 && req->len < 255) {
        memcpy(rsp_pld + 1, req->payload, req->len);
    }

    cmd_frame_t rsp = { .cmd = req->cmd, .sub = cmd_frame_sub_rsp(req->sub),
                        .len = req->len + 1, .payload = rsp_pld };
    cmd_conn_send(conn, &rsp);
}

/**
 * Mock System handler：返回固定版本字符串。
 */
static void mock_handler_system(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx)
{
    (void)ctx;
    const char* ver = "1.0.0-test";
    uint8_t pld[64];
    pld[0] = CMD_ERR_OK;
    size_t vlen = strlen(ver);
    memcpy(pld + 1, ver, vlen);

    cmd_frame_t rsp = { .cmd = req->cmd, .sub = cmd_frame_sub_rsp(req->sub),
                        .len = (uint16_t)(1 + vlen), .payload = pld };
    cmd_conn_send(conn, &rsp);
}

/* ── 服务器线程 ─────────────────────────────────────────────────────── */

static void* server_thread(void* arg)
{
    (void)arg;
    cmd_server_run(g_server);
    return NULL;
}

/* ── 辅助：Unix Socket 客户端 ──────────────────────────────────────── */

static int connect_to_server(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_sock_path, sizeof(addr.sun_path) - 1);

    /* 重试最多 1 秒（服务器可能尚未就绪） */
    for (int i = 0; i < 10; i++) {
        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            return fd;
        }
        usleep(100000);  /* 100ms */
    }
    close(fd);
    return -1;
}

/**
 * 发送一帧并读取响应帧。
 *
 * @return 0 成功收到响应，-1 失败
 */
static int send_and_recv(int fd, const cmd_frame_t* req, cmd_frame_t* rsp_out)
{
    uint8_t buf[512];
    size_t out_len = 0;
    if (cmd_protocol_pack(req, buf, sizeof(buf), &out_len) != 0) {
        return -1;
    }

    ssize_t n = write(fd, buf, out_len);
    if (n != (ssize_t)out_len) return -1;

    /* 读取响应（简单实现：一次 read，预期一帧） */
    uint8_t rx[512];
    n = read(fd, rx, sizeof(rx));
    if (n <= 0) return -1;

    size_t consumed = 0;
    memset(rsp_out, 0, sizeof(*rsp_out));
    int rc = cmd_protocol_parse(rx, (size_t)n, rsp_out, &consumed);
    if (rc != 0) {
        printf("  [parse rc=%d consumed=%zu] ", rc, consumed);
        return -1;
    }

    return 0;
}

/* ── 测试用例 ───────────────────────────────────────────────────────── */

/** 测试 1: LED 写命令往返 */
static void test_led_write(void)
{
    TEST("LED write (roundtrip)");

    int fd = connect_to_server();
    if (fd < 0) { FAIL("connect failed"); return; }

    uint8_t pld[] = {0x01, 0x01};  /* LED#1 开 */
    cmd_frame_t req = { .cmd = CMD_LED, .sub = CMD_SUB_WRITE, .len = 2, .payload = pld };
    cmd_frame_t rsp;
    memset(&rsp, 0, sizeof(rsp));

    if (send_and_recv(fd, &req, &rsp) != 0) { FAIL("send/recv failed"); close(fd); return; }

    if (rsp.cmd != CMD_LED) { FAIL("rsp.cmd mismatch"); goto cleanup; }
    if (rsp.sub != cmd_frame_sub_rsp(CMD_SUB_WRITE)) { FAIL("rsp.sub mismatch"); goto cleanup; }
    if (rsp.len < 1 || rsp.payload[0] != CMD_ERR_OK) { FAIL("error code not OK"); goto cleanup; }
    if (rsp.len >= 3 && memcmp(rsp.payload + 1, pld, 2) != 0) { FAIL("payload not echoed"); goto cleanup; }

    PASS();
cleanup:
    cmd_protocol_free_frame(&rsp);
    close(fd);
}

/** 测试 2: 系统信息查询 */
static void test_system_info(void)
{
    TEST("System info query");

    int fd = connect_to_server();
    if (fd < 0) { FAIL("connect failed"); return; }

    cmd_frame_t req = { .cmd = CMD_SYSTEM, .sub = CMD_SUB_READ, .len = 0, .payload = NULL };
    cmd_frame_t rsp;
    memset(&rsp, 0, sizeof(rsp));

    if (send_and_recv(fd, &req, &rsp) != 0) { FAIL("send/recv failed"); close(fd); return; }

    if (rsp.cmd != CMD_SYSTEM) { FAIL("rsp.cmd mismatch"); goto cleanup; }
    if (rsp.len < 1 || rsp.payload[0] != CMD_ERR_OK) { FAIL("error code not OK"); goto cleanup; }

    /* 检查版本字符串 */
    size_t ver_len = rsp.len - 1;
    char ver[64] = {0};
    memcpy(ver, rsp.payload + 1, ver_len < 63 ? ver_len : 63);
    if (strstr(ver, "1.0.0") == NULL) { FAIL("version string mismatch"); goto cleanup; }

    PASS();
cleanup:
    cmd_protocol_free_frame(&rsp);
    close(fd);
}

/** 测试 3: 未知命令应返回错误 */
static void test_unknown_command(void)
{
    TEST("Unknown command error");

    int fd = connect_to_server();
    if (fd < 0) { FAIL("connect failed"); return; }

    cmd_frame_t req = { .cmd = 0xFF, .sub = CMD_SUB_READ, .len = 0, .payload = NULL };
    cmd_frame_t rsp;
    memset(&rsp, 0, sizeof(rsp));

    if (send_and_recv(fd, &req, &rsp) != 0) { FAIL("send/recv failed"); close(fd); return; }

    if (rsp.len < 1 || rsp.payload[0] != CMD_ERR_UNKNOWN_CMD) {
        FAIL("should return UNKNOWN_CMD error");
        goto cleanup;
    }

    PASS();
cleanup:
    cmd_protocol_free_frame(&rsp);
    close(fd);
}

/** 测试 4: 空 payload 帧 */
static void test_empty_payload(void)
{
    TEST("Empty payload request");

    int fd = connect_to_server();
    if (fd < 0) { FAIL("connect failed"); return; }

    cmd_frame_t req = { .cmd = CMD_LED, .sub = CMD_SUB_READ, .len = 0, .payload = NULL };
    cmd_frame_t rsp;
    memset(&rsp, 0, sizeof(rsp));

    if (send_and_recv(fd, &req, &rsp) != 0) { FAIL("send/recv failed"); close(fd); return; }

    if (rsp.len < 1 || rsp.payload[0] != CMD_ERR_OK) { FAIL("error code not OK"); goto cleanup; }

    PASS();
cleanup:
    cmd_protocol_free_frame(&rsp);
    close(fd);
}

/** 测试 5: 多连接并发 */
static void test_multiple_connections(void)
{
    TEST("Multiple concurrent connections");

    int fds[3];
    for (int i = 0; i < 3; i++) {
        fds[i] = connect_to_server();
        if (fds[i] < 0) { FAIL("connect failed"); return; }
    }

    /* 每个连接发一条不同命令 */
    for (int i = 0; i < 3; i++) {
        cmd_frame_t req = { .cmd = CMD_SYSTEM, .sub = CMD_SUB_READ, .len = 0, .payload = NULL };
        cmd_frame_t rsp;
        memset(&rsp, 0, sizeof(rsp));
        if (send_and_recv(fds[i], &req, &rsp) != 0) { FAIL("send/recv failed"); goto cleanup; }
        if (rsp.payload[0] != CMD_ERR_OK) { FAIL("error code not OK"); goto cleanup; }
        cmd_protocol_free_frame(&rsp);
    }

    PASS();
cleanup:
    for (int i = 0; i < 3; i++) close(fds[i]);
}

/* ── 服务器回调（桥接 server → dispatcher）─────────────────────────── */

static void on_request(const cmd_frame_t* req, cmd_conn_t* conn, void* user_data)
{
    (void)user_data;
    cmd_dispatcher_dispatch(g_dispatcher, req, conn);
}

/* ── 入口 ───────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== cmd module integration tests ===\n\n");

    /* 初始化日志 */
    log_init("/tmp/test_cmd_integration.log", LOG_ERROR);

    /* 创建命令模块基础设施 */
    g_sub_mgr = cmd_subscription_create();
    g_dispatcher = cmd_dispatcher_create(g_sub_mgr);
    cmd_dispatcher_register(g_dispatcher, CMD_LED,    mock_handler_led,    NULL);
    cmd_dispatcher_register(g_dispatcher, CMD_SYSTEM, mock_handler_system, NULL);

    /* 创建服务器 + 注册监听 */
    g_server = cmd_server_create();
    cmd_server_set_handler(g_server, on_request, g_dispatcher);

    /* 注意：cmd_server_set_handler 的类型是 cmd_request_fn，需要包装 dispatcher */
    unlink(g_sock_path);
    int listen_fd = cmd_transport_listen_unix(g_sock_path);
    if (listen_fd < 0) {
        printf("FATAL: cannot create listen socket\n");
        return 1;
    }
    cmd_server_add_listener(g_server, listen_fd);

    /* 启动服务器线程 */
    pthread_t tid;
    pthread_create(&tid, NULL, server_thread, NULL);

    /* 等待服务器就绪 */
    usleep(200000);

    /* 运行测试 */
    test_led_write();
    test_system_info();
    test_unknown_command();
    test_empty_payload();
    test_multiple_connections();

    /* 关闭服务器 */
    cmd_server_stop(g_server);
    pthread_join(tid, NULL);

    /* 清理 */
    cmd_server_destroy(g_server);
    cmd_dispatcher_destroy(g_dispatcher);
    cmd_subscription_destroy(g_sub_mgr);
    unlink(g_sock_path);
    log_deinit();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
