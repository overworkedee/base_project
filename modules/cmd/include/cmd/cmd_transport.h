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
