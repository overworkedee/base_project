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

/**
 * 创建并监听 Unix Domain Socket。
 *
 * 自动 unlink 旧文件、bind、listen(32) 后返回监听 fd。
 *
 * @param path  socket 文件路径，如 "/tmp/cmd.sock"
 * @return      成功返回 fd（≥0），失败返回 -1
 */
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

/**
 * 创建并监听 TCP Socket。
 *
 * 绑定 0.0.0.0:port，设置 SO_REUSEADDR 避免 TIME_WAIT 导致 bind 失败。
 *
 * @param port  监听端口号
 * @return      成功返回 fd（≥0），失败返回 -1
 */
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

/**
 * 阻塞接受一个新连接。
 *
 * @param listen_fd  监听文件描述符
 * @return           成功返回客户端 fd（≥0），失败返回 -1
 */
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

/**
 * 关闭 socket 连接。
 *
 * @param fd  待关闭的文件描述符（负数安全）
 */
void cmd_transport_close(int fd)
{
    if (fd >= 0) {
        LOG_DEBUG("Closing connection fd=%d", fd);
        close(fd);
    }
}
