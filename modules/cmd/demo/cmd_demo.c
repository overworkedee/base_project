/**
 * cmd_demo.c — 命令模块交互式 Shell 客户端
 *
 * 编译: 由 CMD_BUILD_DEMO 环境变量控制
 * 运行: ./out/bin/cmd_demo
 *
 * 通过 Unix Socket 或 TCP 连接 project_app，将文本命令翻译为
 * 二进制帧发送，解析响应并显示。
 */

#define _GNU_SOURCE
#include "cmd/cmd_protocol.h"
#include "cmd/cmd_frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ctype.h>

/* ── 常量 ───────────────────────────────────────────────────────────── */

#define MAX_INPUT    256

/* ── 连接状态 ───────────────────────────────────────────────────────── */

static int  g_fd = -1;              /* 当前 socket fd                  */
static int  g_conn_type = 0;        /* 0=未连接, 1=Unix, 2=TCP        */
static char g_conn_target[256];     /* 连接目标字符串（路径或地址）    */

/* ── 十六进制工具 ───────────────────────────────────────────────────── */

/**
 * 打印十六进制数据。
 */
static void hex_dump(const char* label, const uint8_t* data, size_t len)
{
    printf("  %s [%zu]: ", label, len);
    for (size_t i = 0; i < len; i++) printf("%02X ", data[i]);
    printf("\n");
}

/**
 * 将十六进制字符串转为字节数组，返回字节数。
 *
 * @param hex  十六进制字符串（如 "A5 5A 00 00 02 02"）
 * @param buf  输出缓冲区
 * @param cap  缓冲区容量
 * @return     实际转换的字节数，-1 表示格式错误
 */
static int hex_to_bytes(const char* hex, uint8_t* buf, size_t cap)
{
    size_t count = 0;
    const char* p = hex;

    while (*p && count < cap) {
        /* 跳过空格 */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        /* 读取两个 hex 字符 */
        if (!isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1])) {
            return -1;
        }

        unsigned int byte;
        sscanf(p, "%2x", &byte);
        buf[count++] = (uint8_t)byte;
        p += 2;
    }

    return (int)count;
}

/* ── 帧收发 ─────────────────────────────────────────────────────────── */

/**
 * 发送一帧并等待响应。
 *
 * @param req  请求帧
 * @return     0 成功并打印响应，-1 失败
 */
static int send_frame(const cmd_frame_t* req)
{
    if (g_fd < 0) {
        printf("[ERR] Not connected. Use 'connect unix <path>' or 'connect tcp <host> <port>' first.\n");
        return -1;
    }

    uint8_t buf[2048];
    size_t out_len = 0;

    if (cmd_protocol_pack(req, buf, sizeof(buf), &out_len) != 0) {
        printf("[ERR] Frame pack failed (payload too large?)\n");
        return -1;
    }

    printf("  TX: ");
    hex_dump("", buf, out_len);

    ssize_t n = write(g_fd, buf, out_len);
    if (n != (ssize_t)out_len) {
        printf("[ERR] Write failed: %s\n", strerror(errno));
        return -1;
    }

    /* 读取响应 */
    uint8_t rx[2048];
    n = read(g_fd, rx, sizeof(rx));
    if (n <= 0) {
        printf("[ERR] Read failed (server closed?): %s\n", n == 0 ? "EOF" : strerror(errno));
        close(g_fd);
        g_fd = -1;
        g_conn_type = 0;
        return -1;
    }

    printf("  RX: ");
    hex_dump("", rx, (size_t)n);

    /* 解析并显示帧 */
    cmd_frame_t rsp;
    memset(&rsp, 0, sizeof(rsp));
    size_t consumed = 0;
    int rc = cmd_protocol_parse(rx, (size_t)n, &rsp, &consumed);

    if (rc == 0) {
        const char* cmd_name = "???";
        switch (rsp.cmd) {
        case CMD_LED:    cmd_name = "LED";    break;
        case CMD_SENSOR: cmd_name = "SENSOR"; break;
        case CMD_SYSTEM: cmd_name = "SYSTEM"; break;
        }

        const char* sub_type = (rsp.sub & CMD_SUB_RESPONSE_FLAG) ? "RSP" : "REQ";
        uint8_t op = cmd_frame_sub_req(rsp.sub);

        printf("  ── [%s %s sub=0x%02X len=%u]\n", cmd_name, sub_type, op, rsp.len);

        if (rsp.len > 0 && rsp.payload) {
            uint8_t err = rsp.payload[0];
            const char* err_str = "?";
            switch (err) {
            case CMD_ERR_OK:            err_str = "OK"; break;
            case CMD_ERR_UNKNOWN_CMD:   err_str = "UNKNOWN_CMD"; break;
            case CMD_ERR_UNKNOWN_SUB:   err_str = "UNKNOWN_SUB"; break;
            case CMD_ERR_PARAM:         err_str = "PARAM_ERR"; break;
            case CMD_ERR_HARDWARE:      err_str = "HW_ERR"; break;
            case CMD_ERR_BUSY:          err_str = "BUSY"; break;
            }

            printf("  ── Error: %s (0x%02X)\n", err_str, err);

            /* 特定命令的 payload 解析 */
            if (err == CMD_ERR_OK) {
                if (rsp.cmd == CMD_SENSOR && rsp.len >= 9) {
                    /* 温度 + 湿度 */
                    uint32_t tmp;
                    memcpy(&tmp, rsp.payload + 1, 4);
                    float temp = 0.0f;
                    uint32_t host_tmp = ntohl(tmp);
                    memcpy(&temp, &host_tmp, 4);

                    memcpy(&tmp, rsp.payload + 5, 4);
                    float hum = 0.0f;
                    host_tmp = ntohl(tmp);
                    memcpy(&hum, &host_tmp, 4);

                    printf("  ── Temperature: %.1f °C, Humidity: %.1f %%RH\n", temp, hum);
                } else if (rsp.cmd == CMD_SYSTEM && rsp.len > 1) {
                    printf("  ── Version: %.*s\n", rsp.len - 1, rsp.payload + 1);
                } else if (rsp.cmd == CMD_LED && rsp.len >= 3) {
                    printf("  ── LED #%d: %s\n", rsp.payload[1],
                           rsp.payload[2] ? "ON" : "OFF");
                }
            }
        }
        cmd_protocol_free_frame(&rsp);
    } else if (rc == 1) {
        printf("  ── (incomplete frame, waiting for more data)\n");
    } else {
        printf("  ── (frame parse error)\n");
    }

    return 0;
}

/* ── connect ────────────────────────────────────────────────────────── */

static void cmd_connect(const char* type, const char* arg1, const char* arg2)
{
    if (!type) {
        printf("[CONN] usage: connect <unix|tcp> <path|host> [port]\n");
        return;
    }

    /* 先断开旧连接 */
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
        g_conn_type = 0;
    }

    if (strcmp(type, "unix") == 0) {
        if (!arg1) {
            printf("[CONN] usage: connect unix <socket_path>\n");
            return;
        }

        g_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (g_fd < 0) { printf("[ERR] socket() failed\n"); return; }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, arg1, sizeof(addr.sun_path) - 1);

        if (connect(g_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            printf("[ERR] connect to %s failed: %s\n", arg1, strerror(errno));
            close(g_fd);
            g_fd = -1;
            return;
        }

        g_conn_type = 1;
        snprintf(g_conn_target, sizeof(g_conn_target), "unix:%s", arg1);
        printf("[CONN] Connected to %s (fd=%d)\n", g_conn_target, g_fd);

    } else if (strcmp(type, "tcp") == 0) {
        if (!arg1) {
            printf("[CONN] usage: connect tcp <host> <port>\n");
            return;
        }

        uint16_t port = arg2 ? (uint16_t)atoi(arg2) : 9527;

        g_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (g_fd < 0) { printf("[ERR] socket() failed\n"); return; }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port);
        if (inet_pton(AF_INET, arg1, &addr.sin_addr) <= 0) {
            printf("[ERR] Invalid address: %s\n", arg1);
            close(g_fd);
            g_fd = -1;
            return;
        }

        if (connect(g_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            printf("[ERR] connect to %s:%u failed: %s\n", arg1, port, strerror(errno));
            close(g_fd);
            g_fd = -1;
            return;
        }

        g_conn_type = 2;
        snprintf(g_conn_target, sizeof(g_conn_target), "tcp:%s:%u", arg1, port);
        printf("[CONN] Connected to %s (fd=%d)\n", g_conn_target, g_fd);

    } else {
        printf("[CONN] unknown transport '%s', use 'unix' or 'tcp'\n", type);
    }
}

/* ── LED ────────────────────────────────────────────────────────────── */

static void cmd_led(const char* subcmd, const char* arg)
{
    if (!subcmd) {
        printf("[LED] usage: led <on|off|read> [led_id]\n");
        return;
    }

    uint8_t led_id = arg ? (uint8_t)atoi(arg) : 1;
    cmd_frame_t req;
    memset(&req, 0, sizeof(req));
    req.cmd = CMD_LED;

    if (strcmp(subcmd, "on") == 0) {
        uint8_t pld[] = {led_id, 0x01};
        req.sub     = CMD_SUB_WRITE;
        req.len     = 2;
        req.payload = pld;
        send_frame(&req);
    } else if (strcmp(subcmd, "off") == 0) {
        uint8_t pld[] = {led_id, 0x00};
        req.sub     = CMD_SUB_WRITE;
        req.len     = 2;
        req.payload = pld;
        send_frame(&req);
    } else if (strcmp(subcmd, "read") == 0) {
        uint8_t pld[] = {led_id};
        req.sub     = CMD_SUB_READ;
        req.len     = 1;
        req.payload = pld;
        send_frame(&req);
    } else if (strcmp(subcmd, "heartbeat") == 0) {
        /* heartbeat 通过 trigger 控制，暂映射为 on */
        uint8_t pld[] = {led_id, 0x01};
        req.sub     = CMD_SUB_WRITE;
        req.len     = 2;
        req.payload = pld;
        send_frame(&req);
    } else {
        printf("[LED] unknown: '%s', try on/off/read\n", subcmd);
    }
}

/* ── Sensor ──────────────────────────────────────────────────────────── */

static void cmd_sensor(const char* subcmd, const char* arg)
{
    if (!subcmd) {
        printf("[SENSOR] usage: sensor <read|temp|humidity|subscribe|unsubscribe> [args]\n");
        return;
    }

    if (strcmp(subcmd, "read") == 0 || strcmp(subcmd, "temp") == 0 ||
        strcmp(subcmd, "humidity") == 0) {
        cmd_frame_t req = { .cmd = CMD_SENSOR, .sub = CMD_SUB_READ, .len = 0, .payload = NULL };
        send_frame(&req);
    } else if (strcmp(subcmd, "subscribe") == 0) {
        if (!arg) {
            printf("[SENSOR] usage: sensor subscribe <temp|humidity> [interval_ms]\n");
            return;
        }

        uint16_t data_id;
        if (strcmp(arg, "temp") == 0) {
            data_id = CMD_DATA_TEMPERATURE;
        } else if (strcmp(arg, "humidity") == 0) {
            data_id = CMD_DATA_HUMIDITY;
        } else {
            printf("[SENSOR] unknown data type '%s', use 'temp' or 'humidity'\n", arg);
            return;
        }

        /* 解析 interval（从下一个参数或默认 1000ms） */
        const char* interval_str = strtok(NULL, " ");
        uint32_t interval = interval_str ? (uint32_t)atoi(interval_str) : 1000;

        uint16_t id_be = htons(data_id);
        uint16_t iv_be = htons((uint16_t)interval);
        uint8_t pld[4];
        memcpy(pld, &id_be, 2);
        memcpy(pld + 2, &iv_be, 2);

        cmd_frame_t req = { .cmd = CMD_SENSOR, .sub = CMD_SUB_SUBSCRIBE,
                            .len = 4, .payload = pld };
        printf("  Subscribing data_id=0x%04X interval=%ums...\n", data_id, interval);
        send_frame(&req);
    } else if (strcmp(subcmd, "unsubscribe") == 0) {
        if (!arg) {
            printf("[SENSOR] usage: sensor unsubscribe <temp|humidity>\n");
            return;
        }

        uint16_t data_id;
        if (strcmp(arg, "temp") == 0) {
            data_id = CMD_DATA_TEMPERATURE;
        } else if (strcmp(arg, "humidity") == 0) {
            data_id = CMD_DATA_HUMIDITY;
        } else {
            printf("[SENSOR] unknown data type '%s'\n", arg);
            return;
        }

        uint16_t id_be = htons(data_id);
        cmd_frame_t req = { .cmd = CMD_SENSOR, .sub = CMD_SUB_UNSUBSCRIBE,
                            .len = 2, .payload = (uint8_t*)&id_be };
        send_frame(&req);
    } else {
        printf("[SENSOR] unknown: '%s'\n", subcmd);
    }
}

/* ── System ──────────────────────────────────────────────────────────── */

static void cmd_system(const char* subcmd, const char* arg)
{
    if (!subcmd) {
        printf("[SYSTEM] usage: system <info|loglevel> [level]\n");
        return;
    }

    if (strcmp(subcmd, "info") == 0) {
        cmd_frame_t req = { .cmd = CMD_SYSTEM, .sub = CMD_SUB_READ,
                            .len = 0, .payload = NULL };
        send_frame(&req);
    } else if (strcmp(subcmd, "loglevel") == 0) {
        if (!arg) {
            printf("[SYSTEM] usage: system loglevel <0-3> (0=DEBUG 1=INFO 2=WARN 3=ERROR)\n");
            return;
        }
        uint8_t pld = (uint8_t)atoi(arg);
        cmd_frame_t req = { .cmd = CMD_SYSTEM, .sub = 0x03,
                            .len = 1, .payload = &pld };
        send_frame(&req);
    } else {
        printf("[SYSTEM] unknown: '%s'\n", subcmd);
    }
}

/* ── Raw ─────────────────────────────────────────────────────────────── */

static void cmd_raw(const char* hex_str)
{
    if (!hex_str) {
        printf("[RAW] usage: raw <hex bytes...>\n");
        printf("       e.g.: raw A5 5A 00 00 02 02\n");
        return;
    }

    uint8_t buf[256];
    int len = hex_to_bytes(hex_str, buf, sizeof(buf));
    if (len < 0) {
        printf("[RAW] Invalid hex format. Use space-separated hex pairs.\n");
        return;
    }

    /* 直接发送原始字节（不是帧） */
    ssize_t n = write(g_fd, buf, (size_t)len);
    printf("  TX raw [%d]: ", len);
    for (int i = 0; i < len; i++) printf("%02X ", buf[i]);
    printf("-> %s\n", n == len ? "OK" : "FAIL");

    /* 尝试读响应 */
    uint8_t rx[2048];
    n = read(g_fd, rx, sizeof(rx));
    if (n > 0) {
        printf("  RX raw [%zd]: ", n);
        for (ssize_t i = 0; i < n; i++) printf("%02X ", rx[i]);
        printf("\n");
    }
}

/* ── 帮助 ───────────────────────────────────────────────────────────── */

static void cmd_help(void)
{
    printf(
        "\n"
        "=== CMD Shell ===\n"
        "\n"
        "  connect unix <path>       — Connect via Unix socket\n"
        "  connect tcp <host> <port> — Connect via TCP (default port 9527)\n"
        "\n"
        "  led on     [id]           — Turn LED on\n"
        "  led off    [id]           — Turn LED off\n"
        "  led read   [id]           — Read LED status\n"
        "\n"
        "  sensor read               — Read temperature + humidity\n"
        "  sensor temp               — Same as read\n"
        "  sensor subscribe temp [ms]— Subscribe temperature (default 1000ms)\n"
        "  sensor unsubscribe temp   — Unsubscribe\n"
        "\n"
        "  system info               — Get system version\n"
        "  system loglevel <0-3>     — Set log level\n"
        "\n"
        "  raw <hex bytes>           — Send raw hex bytes\n"
        "  help                      — Show this help\n"
        "  quit                      — Exit\n"
        "\n"
    );
}

/* ── 主循环 ─────────────────────────────────────────────────────────── */

int main(void)
{
    char input[MAX_INPUT];

    printf("=== CMD Shell Demo ===\n");
    printf("Type 'help' for commands. Connect first, then send commands.\n\n");

    while (1) {
        /* 显示连接状态 */
        if (g_fd >= 0) {
            printf("cmd[%s]> ", g_conn_target);
        } else {
            printf("cmd> ");
        }
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) {
            printf("\n");
            break;
        }

        /* 去除尾部换行 */
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') input[len - 1] = '\0';
        if (len == 0) continue;

        /* 拆 token */
        char* cmd  = strtok(input, " ");
        char* arg1 = strtok(NULL, " ");
        char* arg2 = strtok(NULL, " ");

        if (!cmd) continue;

        if (strcmp(cmd, "help") == 0) {
            cmd_help();
        } else if (strcmp(cmd, "connect") == 0) {
            cmd_connect(arg1, arg2, strtok(NULL, " "));
        } else if (strcmp(cmd, "led") == 0) {
            cmd_led(arg1, arg2);
        } else if (strcmp(cmd, "sensor") == 0) {
            cmd_sensor(arg1, arg2);
        } else if (strcmp(cmd, "system") == 0) {
            cmd_system(arg1, arg2);
        } else if (strcmp(cmd, "raw") == 0) {
            /* raw 命令需要整行十六进制字符串 */
            char* rest = input + strlen(cmd) + 1;
            while (*rest == ' ') rest++;
            cmd_raw(*rest ? rest : NULL);
        } else if (strcmp(cmd, "quit") == 0) {
            printf("Bye.\n");
            break;
        } else {
            printf("Unknown command: '%s', type 'help'\n", cmd);
        }
    }

    if (g_fd >= 0) close(g_fd);
    return 0;
}
