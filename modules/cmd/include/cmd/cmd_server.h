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
