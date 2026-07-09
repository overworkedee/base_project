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
    conn->epoll_fd = s->epoll_fd;
    conn->last_active = _now();
    conn->ctx = NULL;
    hw_mutex_init(&conn->tx_lock);

    /* 加入 epoll */
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events   = EPOLLIN;
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
 *
 * @return 0 连接仍活跃，1 连接已被关闭（调用者不应再访问 conn）
 */
static int _handle_read(cmd_server_t* s, cmd_conn_t* conn)
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
        return 0;
    } else if (n == 0) {
        _close_conn(s, conn);
        return 1;
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_DEBUG("Read error on fd=%d: %s", conn->fd, strerror(errno));
        _close_conn(s, conn);
        return 1;
    }
    return 0;
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
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /* 发送缓冲区满，等下次 EPOLLOUT */
            break;
        } else if (n < 0) {
            /* 写错误，关闭连接 */
            LOG_DEBUG("Write error on fd=%d: %s", conn->fd, strerror(errno));
            hw_mutex_unlock(&conn->tx_lock);
            _close_conn(s, conn);
            return;
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
            uint32_t ev = events[i].events;

            /* 区分监听 fd 和连接 fd：监听 fd 通过 data.fd 识别 */
            int fd = events[i].data.fd;
            int is_listener = 0;
            for (int j = 0; j < s->listener_count; j++) {
                if (s->listener_fds[j] == fd) { is_listener = 1; break; }
            }

            if (is_listener) {
                /* 监听 fd：仅处理 EPOLLIN */
                if (ev & EPOLLIN) _accept_conn(s, fd);
                continue;
            }

            /* 连接 fd：统一处理该 fd 的所有事件，关闭后跳过 */
            cmd_conn_t* conn = (cmd_conn_t*)events[i].data.ptr;
            int closed = 0;

            if (ev & EPOLLIN) {
                closed = _handle_read(s, conn);
            }
            if (!closed && (ev & EPOLLOUT)) {
                _handle_write(s, conn);
            }
            if (!closed && (ev & (EPOLLERR | EPOLLHUP))) {
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

/* ── 连接操作 ───────────────────────────────────────────────────────── */

int cmd_conn_send(cmd_conn_t* conn, const cmd_frame_t* frame)
{
    if (!conn || !frame) return -1;

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
        ev.events   = EPOLLIN | EPOLLOUT;
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
