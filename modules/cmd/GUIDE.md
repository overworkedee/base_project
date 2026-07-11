# CMD 命令模块指南

## 1. 定位

cmd 是 project_app 的**远程控制与监控通道**，允许 PC/网页通过 TCP 或 Unix Socket 发送二进制命令帧来控制硬件、读取传感器、订阅数据流。

## 2. 五层架构

```
┌─────────────────────────────────────────┐
│  Handlers (cmd_handler_*)               │  ← 业务逻辑：LED / 传感器 / 系统
├─────────────────────────────────────────┤
│  Dispatcher (cmd_dispatcher)            │  ← 路由表：CMD → handler 映射
├─────────────────────────────────────────┤
│  Server (cmd_server)                    │  ← epoll 事件循环 + 连接管理
├─────────────────────────────────────────┤
│  Protocol (cmd_protocol)                │  ← 帧打包/拆包 + CRC 校验
├─────────────────────────────────────────┤
│  Transport (cmd_transport)              │  ← socket 监听 (TCP / Unix)
└─────────────────────────────────────────┘
```

**依赖方向：上层依赖下层，下层不知道上层存在。**

## 2.1 完整函数调用链

以下按时间顺序，追踪一个请求从 socket 到达 → 解析 → 分发 → 响应 → 发送的完整路径。

### 启动阶段

```
main()
  ├── app_cmd_create()
  │     ├── cmd_subscription_create()          → sub_mgr
  │     │     └── hw_mutex_init(&mgr->lock)
  │     ├── cmd_dispatcher_create(sub_mgr)     → dispatcher
  │     └── cmd_server_create()                → server
  │           └── epoll_create1(EPOLL_CLOEXEC)
  │
  ├── app_cmd_register(g_cmd, CMD_LED,    cmd_handler_led,    g_led)
  │     └── cmd_dispatcher_register(CMD_LED, handler, ctx)
  │           └── entries[0x01] = {handler, ctx}
  │
  ├── app_cmd_register(g_cmd, CMD_SENSOR, cmd_handler_sensor, &sctx)
  ├── app_cmd_register(g_cmd, CMD_SYSTEM, cmd_handler_system, sub_mgr)
  │
  ├── app_cmd_add_listener_unix(g_cmd, "/tmp/cmd.sock")
  │     ├── cmd_transport_listen_unix(path)
  │     │     ├── socket(AF_UNIX, SOCK_STREAM)
  │     │     ├── bind(fd, ...)
  │     │     └── listen(fd, 32)
  │     └── cmd_server_add_listener(server, fd)
  │           └── epoll_ctl(EPOLL_CTL_ADD, fd, EPOLLIN)
  │
  ├── app_cmd_add_listener_tcp(g_cmd, 9527)
  │     ├── cmd_transport_listen_tcp(9527)
  │     │     ├── socket(AF_INET, SOCK_STREAM)
  │     │     ├── setsockopt(SO_REUSEADDR)
  │     │     ├── bind(fd, 0.0.0.0:9527)
  │     │     └── listen(fd, 32)
  │     └── cmd_server_add_listener(server, fd)
  │
  ├── log_set_subscribe_callback(on_log_push, sub_mgr)
  │     └── g_log_sub_cb = on_log_push  (之后每条 LOG_* 都会触发)
  │
  └── app_cmd_run(g_cmd)
        └── cmd_server_run(server)            ← 进入 epoll 事件循环（阻塞）
```

### 请求处理阶段（从字节到响应）

```
cmd_server_run()                                [主线程]
  │
  └── while (s->running):
        nfds = epoll_wait(epoll_fd, events, 16, 10000ms)
        │
        for each event: ──────────────────────────────────────
        │
        ├── [监听 fd 收到 EPOLLIN]
        │   _accept_conn(s, listen_fd)          cmd_server.c:154
        │     ├── cmd_transport_accept(fd)       cmd_transport.c:95
        │     │     └── accept(fd, NULL, NULL)
        │     ├── calloc(cmd_conn_t)            ← 分配连接结构
        │     ├── hw_mutex_init(&conn->tx_lock)
        │     ├── epoll_ctl(EPOLL_CTL_ADD, client_fd, EPOLLIN)
        │     └── 插入 conn_head 链表
        │
        ├── [连接 fd 收到 EPOLLIN]
        │   closed = _handle_read(s, conn)      cmd_server.c:297
        │     ├── read(conn->fd, conn->rx_buf + rx_len, space)
        │     │     └── 内核 TCP 栈 → 应用层字节流
        │     └── _process_rx(s, conn)          cmd_server.c:241
        │           │
        │           └── while (conn->rx_len > 0):  ← 循环解析粘包
        │                 rc = cmd_protocol_parse(rx_buf, rx_len, &frame, &consumed)
        │                 │                      cmd_protocol.c:58
        │                 │   ├── 扫描 0xA5 0x5A 帧头
        │                 │   ├── 读取 LEN (ntohs)
        │                 │   ├── 检查完整性
        │                 │   ├── cmd_crc8() 校验
        │                 │   └── malloc(payload) + memcpy
        │                 │
        │                 if (rc == 0):         ← 完整帧
        │                   s->handler(&frame, conn, s->handler_data)
        │                   │
        │                   │ 即 app_cmd.c 中的 on_request():
        │                   │   cmd_dispatcher_dispatch(disp, req, conn)
        │                   │                      cmd_dispatcher.c:72
        │                   │     ├── entry = &d->entries[req->cmd]
        │                   │     ├── if (!entry->handler):
        │                   │     │     _send_error(CMD_ERR_UNKNOWN_CMD)
        │                   │     │     └── cmd_conn_send(conn, &err_rsp)
        │                   │     └── entry->handler(req, conn, entry->ctx)
        │                   │           │
        │                   │           ├─ cmd_handler_led()     cmd_handler_led.c
        │                   │           │    led_on(led) / led_off(led)
        │                   │           │    cmd_conn_send(conn, &rsp)
        │                   │           │
        │                   │           ├─ cmd_handler_sensor()  cmd_handler_sensor.c
        │                   │           │    sht30_read_temperature() / sht30_read_humidity()
        │                   │           │    cmd_conn_send()  或  cmd_subscription_add/remove()
        │                   │           │
        │                   │           └─ cmd_handler_system()  cmd_handler_system.c
        │                   │                log_set_level()   或  cmd_subscription_add/remove()
        │                   │                log_ring_get_all() 或  cmd_conn_send()
        │                   │
        │                   cmd_protocol_free_frame(&frame)
        │                   memmove() 移除已消费字节
        │
        ├── [连接 fd 收到 EPOLLOUT]
        │   _handle_write(s, conn)              cmd_server.c:325
        │     ├── hw_mutex_lock(&conn->tx_lock)
        │     ├── while (conn->tx_head):
        │     │     write(conn->fd, node->data + sent, remain)
        │     │     if 全部发送: free(node) 摘除链表
        │     │     else:         break (等下次 EPOLLOUT)
        │     └── hw_mutex_unlock(&conn->tx_lock)
        │
        └── [超时检查，每 10s]
            _check_idle(s)                      cmd_server.c:375
              └── 遍历 conn_head:
                    若 now - conn->last_active > 60s:
                      _close_conn(s, conn)
                        ├── epoll_ctl(EPOLL_CTL_DEL)
                        ├── close(fd)
                        ├── 链表摘除
                        ├── 释放 tx_queue
                        └── free(conn)
```

### cmd_conn_send 的完整路径（handler → 网络）

```
cmd_handler_led() 中:
  cmd_conn_send(conn, &rsp)                   cmd_server.c:461
    │
    ├── cmd_protocol_pack(&rsp, data, total, &out_len)
    │                                          cmd_protocol.c:19
    │     ├── htons(frame->len)               ← BE 序转换
    │     ├── memcpy HEAD + LEN + CMD + SUB + PAYLOAD
    │     └── cmd_crc8(buf, total-1)          ← XOR all
    │
    ├── malloc(tx_node)                       ← 创建发送节点
    │
    ├── hw_mutex_lock(&conn->tx_lock)         ← 线程安全
    ├── 加入 conn->tx_tail 链表
    ├── if (was_empty):
    │     epoll_ctl(EPOLL_CTL_MOD, EPOLLIN|EPOLLOUT)  ← 激活发送
    └── hw_mutex_unlock(&conn->tx_lock)
```

### 订阅生命周期（完整）

```
[订阅]
客户端帧: CMD=SENSOR, SUB=SUBSCRIBE, PAYLOAD=[data_id BE, interval BE]
  │
  _process_rx() 中 s->handler 回调
    → cmd_dispatcher_dispatch(disp, req, conn)
      → cmd_handler_sensor(req, conn, ctx)
          op=SUBSCRIBE:
            ntohs(data_id), ntohs(interval)
            cmd_subscription_add(sub_mgr, data_id, interval, conn)
                                               cmd_subscription.c:107
              ├── hw_mutex_lock(&mgr->lock)
              ├── _find_stream(mgr, data_id)   ← 查找已有 data_stream
              │     └── 遍历 mgr->streams_head 链表
              ├── if (相同 data_id+conn 已存在):
              │     sn->interval_ms = interval ← 更新间隔
              │     return 0
              ├── _ensure_stream(mgr, data_id) ← 不存在则创建
              │     ├── calloc(data_stream_t)
              │     └── 插入 streams_head 头部
              ├── calloc(sub_node_t)
              ├── sn->conn = conn
              ├── sn->interval_ms = interval
              ├── sn->next = ds->subs_head    ← 插入订阅者链表头部
              └── hw_mutex_unlock(&mgr->lock)

[推送]
sensor_thread 中:
  sht30_read_temperature(&temp_c)
  htonl(temp_c)
  cmd_subscription_push(sub_mgr, CMD_SENSOR, CMD_DATA_TEMPERATURE, val, 4)
                                               cmd_subscription.c:182
    ├── hw_mutex_lock(&mgr->lock)
    ├── _find_stream(mgr, data_id)
    ├── if (!ds || !ds->subs_head): return 0 ← 无订阅者
    ├── 组帧:
    │     pld = [data_id 2B BE | value N B]
    │     frame.cmd = CMD_SENSOR
    │     frame.sub = cmd_frame_sub_rsp(CMD_SUB_SUBSCRIBE) = 0x83
    ├── while (sn = ds->subs_head ...):
    │     cmd_conn_send(sn->conn, &frame)     ← 发给每个订阅者
    │     count++
    └── hw_mutex_unlock(&mgr->lock)

[取消]
客户端帧: CMD=SENSOR, SUB=UNSUBSCRIBE, PAYLOAD=[data_id BE]
  cmd_handler_sensor() → cmd_subscription_remove(sub_mgr, data_id, conn)
    ├── hw_mutex_lock(&mgr->lock)
    ├── _find_stream(data_id)
    ├── 遍历 ds->subs_head, 匹配 conn
    ├── 链表摘除 + free(sub_node_t)
    └── hw_mutex_unlock(&mgr->lock)
```

### 日志流的完整路径（LOG_* 宏 → 上位机）

```
任意代码中:
  LOG_INFO("SHT30 sensor initialized at %s", path)
    └── LOG_WRITE(LOG_INFO, fmt, ...)
          └── log_write_impl(LOG_INFO, __FILE__, __LINE__, __func__, fmt, ...)
                                                log.c:200
              │
              ├── _make_timestamp()             ← 锁外，避免系统调用在临界区
              │
              ├── hw_mutex_lock(&g_log.lock)
              │
              ├── 等级过滤
              │
              ├── snprintf(header) + vsnprintf(msg)  ← 格式化: [时间][等级][文件:行 函数]
              │
              ├── fprintf(g_log.fp,     "%s%s\n", header, msg)  ← 写日志文件
              ├── fprintf(stdout,       "%s%s\n", header, msg)  ← 写终端
              │
              ├── _log_ring_push(level, msg)      ← 环形缓冲区
              │     ├── g_log_ring[head] = {level, time(NULL), msg}
              │     └── head = (head+1) % LOG_RING_SIZE
              │
              ├── if (g_log_sub_cb):
              │     g_log_sub_cb(level, msg, g_log_sub_ctx)
              │     │
              │     │ 即 main.c 中的 on_log_push():          main.c
              │     │   ├── 构造数据: [level|reserved|timestamp LE|msg]
              │     │   └── cmd_subscription_push(sub_mgr, CMD_SYSTEM,
              │     │          CMD_DATA_LOG, buf, data_len)
              │     │         └── 遍历 CMD_DATA_LOG 的订阅者 → cmd_conn_send
              │     │
              └── hw_mutex_unlock(&g_log.lock)

[首次订阅时的历史回放]
cmd_handler_system() → op=CMD_SUB_LOG_SUBSCRIBE:
  cmd_subscription_add(sub_mgr, CMD_DATA_LOG, 0, conn)  ← interval=0 事件驱动
  │
  log_ring_get_all(entries, &count)                      log.c
  │   ├── hw_mutex_lock(&g_log.lock)
  │   ├── 从 head-count 处开始，环形遍历复制
  │   └── hw_mutex_unlock(&g_log.lock)
  │
  for (i=0; i<count && i<500; i++):
      构造推送帧 → cmd_conn_send(conn, &push)             ← 逐条发给新订阅者
```

## 2.2 模块依赖矩阵

```
              ┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
              │transport │ protocol │  server  │dispatcher│subscript │ handler  │
├─────────────┼──────────┼──────────┼──────────┼──────────┼──────────┼──────────┤
│ transport   │    -     │          │          │          │          │          │
│ protocol    │          │    -     │          │          │          │          │
│ server      │  accept  │ pack(发) │    -     │ request  │          │  send    │
│             │  创建fd  │ parse(收)│          │  cb调起  │          │  队列    │
│ dispatcher  │          │          │          │    -     │  提供    │ 调用     │
│             │          │          │          │          │ sub_mgr  │ handler  │
│ subscription│          │ 组推送帧 │ cmd_conn │          │    -     │          │
│             │          │ pack()   │  _send   │          │          │          │
│ handler     │          │ 组响应帧 │ cmd_conn │          │  add/rm  │    -     │
│             │          │ pack()   │  _send   │          │ 推送     │          │
└─────────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────┘

线程安全边界:
  ┌─ 主线程（epoll event loop）────────────────────────────────┐
  │  _accept_conn / _handle_read / _handle_write / _check_idle │
  │  _process_rx → dispatcher → handler → cmd_conn_send        │
  └────────────────────────────────────────────────────────────┘
  ┌─ sensor_thread ────────────────────────────────────────────┐
  │  sht30_read → cmd_subscription_push → cmd_conn_send        │
  └────────────────────────────────────────────────────────────┘
  ┌─ 任意线程调用 LOG_* ───────────────────────────────────────┐
  │  log_write_impl → _log_ring_push → on_log_push             │
  │                → cmd_subscription_push → cmd_conn_send      │
  └────────────────────────────────────────────────────────────┘

  并发保护:
    cmd_conn_send()      → conn->tx_lock  (每连接独立锁)
    cmd_subscription_*() → mgr->lock      (全局互斥锁)
    log_write_impl()     → g_log.lock     (日志模块锁)
```

## 3. 二进制帧格式

```
┌───────┬───────┬───────────┬───────┬───────┬─────────────┬───────┐
│ HEAD  │ HEAD  │ LEN       │ CMD   │ SUB   │ PAYLOAD     │ CRC   │
│ 0xA5  │ 0x5A  │ 2B BE     │ 1B    │ 1B    │ 0~65535 B   │ 1B    │
└───────┴───────┴───────────┴───────┴───────┴─────────────┴───────┘
  ◄────────── 6 字节固定头 ──────────►  ◄── LEN 字节 ──►  ◄ XOR ─►
```

- **HEAD**: 帧同步魔数 `0xA5 0x5A`，接收端扫描这两个字节定位帧起点
- **LEN**: payload 长度，大端序（`htons`/`ntohs`），最大 65535 字节
- **CMD**: 命令大类。`0x01=LED` `0x02=传感器` `0x03=系统`
- **SUB**: 子命令。bit7=0 请求，bit7=1 响应/推送。操作码取低 7 位
- **CRC**: 对 HEAD~PAYLOAD 逐字节 XOR，用于检错

### 子命令操作码（低 7 位）

| 值 | 宏 | 含义 |
|----|-----|------|
| 0x01 | `CMD_SUB_WRITE` | 写操作 |
| 0x02 | `CMD_SUB_READ` | 读操作 |
| 0x03 | `CMD_SUB_SUBSCRIBE` | 订阅数据流 |
| 0x04 | `CMD_SUB_UNSUBSCRIBE` | 取消订阅 |
| 0x05 | `CMD_SUB_LOG_SUBSCRIBE` | 订阅日志推送 |
| 0x06 | `CMD_SUB_LOG_UNSUBSCRIBE` | 取消日志订阅 |

### 当前命令集

| CMD | Handler | 操作 |
|-----|---------|------|
| `0x01` LED | `cmd_handler_led` | WRITE: 开/关 LED（`[led_id, state]`）；READ: 查询状态 |
| `0x02` SENSOR | `cmd_handler_sensor` | READ: 获取温湿度（float BE）；SUBSCRIBE/UNSUBSCRIBE: 温湿度数据流 |
| `0x03` SYSTEM | `cmd_handler_system` | READ: 版本号；sub=0x03: 设置日志等级；LOG_SUBSCRIBE(0x05): 订阅日志；LOG_UNSUBSCRIBE(0x06): 取消 |

## 4. 帧同步策略

接收端 TCP 是字节流，可能粘包/半包/垃圾数据。`cmd_protocol_parse()` 的处理流程：

```
字节流 → 扫描 0xA5 0x5A 找帧头
       → 读 LEN（2B BE）
       → 检查是否收到足够字节（HEADER+LEN+CRC）
       → 计算 CRC（HEAD~PAYLOAD 全部字节的 XOR）
       → CRC 匹配 → 返回完整帧
       → CRC 不匹配 → 跳过 1 字节，重新扫描
```

**三种返回值**：
- `0` — 解析成功，frame 已填充
- `1` — 数据不完整，需要更多字节
- `-1` — 未找到有效帧头或 CRC 失败（调用方应丢弃缓冲区或跳过）

## 5. 订阅机制（核心）

### 5.1 数据结构：二维链表

```
cmd_subscription_mgr_t
  │
  ├── lock (hw_mutex_t)          ← 全局锁，所有操作互斥
  │
  └── streams_head
        │
        ├── data_stream_t { data_id=0x0001 "温度" }
        │     └── subs_head → sub_node_t(conn=A, interval=1000ms)
        │                   → sub_node_t(conn=B, interval=500ms)
        │
        ├── data_stream_t { data_id=0x0002 "湿度" }
        │     └── subs_head → sub_node_t(conn=A, interval=1000ms)
        │
        └── data_stream_t { data_id=0x0003 "日志" }
              └── subs_head → sub_node_t(conn=C, interval=0)
```

- `data_stream_t`：一个数据流 ID 对应一条记录
- `sub_node_t`：订阅者 = 连接 + 推送间隔（interval=0 表示事件驱动）
- 相同 `(data_id, conn)` 重复 `add` 视为更新 `interval_ms`
- **线程安全**：所有 API 内部持有 `hw_mutex_t` 互斥锁

### 5.2 订阅流程（以温度为例）

```
客户端                                         服务端
  │                                              │
  │ CMD=0x02, SUB=0x03, [{data_id=0x0001, interval=1000}]  帧
  │─────────────────────────────────────────────►│
  │                                              │ cmd_server epoll 收帧
  │                                              │ cmd_dispatcher → cmd_handler_sensor
  │                                              │ cmd_subscription_add(sub_mgr, 0x0001, 1000, conn)
  │                                              │   → 创建/更新 subscription 链表节点
  │                                              │
  │ ◄─────── CMD_ERR_OK 确认帧 ─────────────────│
  │                                              │
  │   ... sensor_thread 每秒采集一次 ...         │
  │                                              │ sensor_thread:
  │                                              │   sht30_read_temperature(28.3°C)
  │                                              │   封装 value=[28.3f BE]
  │                                              │   cmd_subscription_push(sub_mgr, CMD_SENSOR, 0x0001, value, 4)
  │                                              │     → lock → 找到 data_stream → 遍历 subs_head
  │                                              │     → 组推送帧: CMD=0x02, SUB=0x83
  │                                              │       PAYLOAD=[data_id 2B BE | value 4B BE]
  │                                              │     → cmd_conn_send(conn_A, &frame)
  │                                              │     → cmd_conn_send(conn_B, &frame)
  │ ◄─────── 推送帧（温度 28.3） ──────────────│
  │ ◄─────── 推送帧（温度 28.3） ──────────────│
  │                                              │
```

### 5.3 日志订阅的特殊之处

日志不通过定时器推送，而是 **事件驱动**（每条 LOG_* 宏触发）：

```
log_write_impl()
  ├── fprintf 写文件
  ├── _log_ring_push()  写入环形缓冲区（2048 条）
  └── if (g_log_sub_cb)
        g_log_sub_cb()  ← 即 main.c 中的 on_log_push()
          └── cmd_subscription_push(sub_mgr, CMD_SYSTEM, CMD_DATA_LOG, data, len)
                └── 遍历订阅者链表 → cmd_conn_send 推送
```

**首次订阅时的历史回放**：`cmd_handler_system` 收到 `LOG_SUBSCRIBE` 后，先调用 `log_ring_get_all()` 读取环形缓冲区中已缓存的日志（最多 500 条），逐条发给新订阅者，再注册实时推送。

## 6. 连接管理（cmd_server）

### 6.1 epoll 事件循环

```
while (server->running):
  nfds = epoll_wait(epoll_fd, events, 16, timeout=10s)

  for each event:
    if fd 是监听 socket:
      → _accept_conn() 接受新连接
    else (fd 是客户端连接):
      EPOLLIN  → _handle_read()  → read() → _process_rx() → 解析帧 → 调用 handler
      EPOLLOUT → _handle_write() → 遍历 tx_queue 链表，write() 发送待发数据
      EPOLLERR/EPOLLHUP → _close_conn() 关闭连接

  _check_idle()  // 每 10s 扫描一次，踢出 60s 无活动的连接
```

**关键设计**：
- **Level-triggered**（非边缘触发），避免丢事件
- 每个连接独立 `rx_buf[4096]` 和 `tx_queue`（链表）
- `cmd_conn_send()` 线程安全（内部加 `tx_lock`），可从 handler 或任意线程调用
- 连接关闭时自动调用 `cmd_subscription_remove_all()` 清理订阅

### 6.2 发送队列

```
cmd_conn_send(conn, frame)
  ├── cmd_protocol_pack() 序列化为字节流
  ├── hw_mutex_lock(&conn->tx_lock)
  ├── 创建 tx_node → 加入 tx_tail 链表尾部
  ├── epoll_ctl(EPOLL_CTL_MOD) 注册 EPOLLOUT
  └── hw_mutex_unlock(&conn->tx_lock)

下次 epoll_wait 检测到 EPOLLOUT:
  _handle_write(conn)
    ├── hw_mutex_lock(&conn->tx_lock)
    ├── 遍历 tx_head 链表，write() 逐节点发送
    ├── 全部写完 → 取消 EPOLLOUT
    └── hw_mutex_unlock(&conn->tx_lock)
```

## 7. 扩展新命令

只需 3 步，**零侵入现有代码**：

```c
// 1. cmd_frame.h 加宏
#define CMD_MOTOR  0x04

// 2. 写 handler（新文件 cmd_handler_motor.c）
void cmd_handler_motor(const cmd_frame_t* req, cmd_conn_t* conn, void* ctx) {
    uint8_t op = cmd_frame_sub_req(req->sub);
    if (op == CMD_SUB_WRITE) { /* ... */ }
    else if (op == CMD_SUB_READ) { /* ... */ }
    else { /* 回复 CMD_ERR_UNKNOWN_SUB */ }
}

// 3. main.c 注册
app_cmd_register(g_cmd, CMD_MOTOR, cmd_handler_motor, motor_ctx);
```

## 8. 关键设计决策

| 决策 | 原因 |
|------|------|
| 帧头 0xA5 0x5A | 字节流中唯一性高，扫描开销低 |
| CRC 用 XOR 而非标准 CRC8 | 简单，对随机比特翻转有基本检测力 |
| epoll level-triggered | 避免 ET 模式下的 starvation 和复杂缓冲 |
| 订阅走链表而非哈希表 | 订阅者数量少（<100），链表足够且无哈希冲突 |
| handler ctx 用 void* | 每个 CMD 大类自由定义上下文类型，最大灵活 |
| app_cmd_register 逐条注册 | vs 一次传所有指针：新增命令只需加一行，不用改函数签名 |
