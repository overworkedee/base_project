# 命令模块 (cmd) 设计文档

## 概述

为 `project_app` 增加一个二进制协议的命令模块，允许外部程序（上位机、网页、脚本等）通过 Unix Socket 或 TCP Socket 发送命令来控制硬件、查询传感器数据，并支持周期性的主动数据推送。

## 设计目标

- **分层解耦**：传输层、协议层、调度层、处理器各司其职，通过清晰接口通信
- **双传输**：同时支持 Unix Socket（本地）和 TCP Socket（远程）
- **请求-响应 + 主动推送**：一手动命令，二周期性数据上报，通过订阅机制实现
- **高实时性**：混合并发模型，epoll 处理网络 I/O，工作线程处理耗时的硬件操作
- **线程安全**：基于现有 `hw_mutex`，共享资源加锁访问

---

## 分层架构

```
┌─────────────────────────────────────────────┐
│              上位机 / 网页 / 脚本             │
├─────────────────────────────────────────────┤
│  传输层 (cmd_transport)                      │
│  ├── Unix Socket (本地)                      │
│  └── TCP Socket (远程)                       │
├─────────────────────────────────────────────┤
│  协议层 (cmd_protocol)                       │
│  ├── 帧解析 / 组包                           │
│  ├── CRC 校验                                │
│  └── 超时 / 粘包处理                         │
├─────────────────────────────────────────────┤
│  服务层 (cmd_server)                         │
│  ├── epoll 事件循环                          │
│  ├── 连接管理 (accept / close)               │
│  └── 回调注册 (request / disconnect)         │
├─────────────────────────────────────────────┤
│  调度层 (cmd_dispatcher)                     │
│  ├── 大类 → 处理器路由表                     │
│  ├── 请求-响应匹配                           │
│  └── 订阅/推送管理 (cmd_subscription)        │
├─────────────────────────────────────────────┤
│  命令处理器 (cmd_handler_*)                  │
│  ├── led_handler       (0x01)               │
│  ├── sensor_handler    (0x02)               │
│  └── system_handler    (0x03)               │
├─────────────────────────────────────────────┤
│              项目业务层                       │
└─────────────────────────────────────────────┘
```

---

## 二进制帧协议

### 帧格式

```
┌──────┬──────┬──────┬──────────┬──────────────┬──────┐
│ HEAD │ LEN  │ CMD  │ SUB_CMD  │   PAYLOAD    │ CRC  │
│ 2B   │ 2B   │ 1B   │ 1B       │   0~65535 B  │ 1B   │
└──────┴──────┴──────┴──────────┴──────────────┴──────┘
```

| 字段 | 大小 | 说明 |
|---|---|---|
| HEAD | 2B | 帧头魔数 `0xA5 0x5A`，用于字节流中的帧同步 |
| LEN | 2B | PAYLOAD 长度（大端序），范围 0~65535 |
| CMD | 1B | 命令大类 |
| SUB | 1B | 子命令，bit7 为方向位（0=请求，1=响应/推送） |
| PAYLOAD | LEN | 变长负载 |
| CRC | 1B | HEAD 到 PAYLOAD 末字节的 XOR 校验和 |

单帧最大长度：65542 字节（≈64KB）。

**字节序**：HEAD/LEN/CMD/SUB/CRC 均为单字节或双字节字段，LEN 使用大端序；PAYLOAD 内多字节字段（data_id、interval_ms、float 等）统一使用大端序。

### SUB 字段编码

```
bit7              bit6 ~ bit0
 ┬                  └──────────
 │                      操作码
 └─ 方向: 0=请求, 1=响应/推送
```

操作码定义：
- `0x01` 写（Write）
- `0x02` 读（Read）
- `0x03` 订阅（Subscribe）
- `0x04` 取消订阅（Unsubscribe）

请求 SUB=0x01 → 响应 SUB=0x81

### 帧同步策略

1. 扫描字节流，找到 `0xA5`
2. 检查下一字节是否为 `0x5A`，不是则丢弃 `0xA5` 继续扫描
3. 读取 LEN（2 字节大端），确定需读取的字节数
4. 读取 LEN+1 字节（PAYLOAD + CRC）
5. 计算 XOR 校验，匹配则帧完整，不匹配则丢弃并从下一个 `0xA5` 重新同步

### 示例帧

**打开 1 号 LED（写操作）：**
```
A5 5A  00 01  01  01  01  XX
头      LEN=1  LED 写  LED#1 开   CRC
```

**读温度传感器：**
```
A5 5A  00 00  02  02  XX
头      LEN=0  传感器 读  CRC
```

**读温度响应（25.0°C）：**
```
A5 5A  00 04  02  82  41 C8 00 00  XX
头      LEN=4  传感器 读响应  float=25.0  CRC
```

**订阅温湿度（间隔 1000ms）：**
```
A5 5A  00 04  02  03  00 01  00 00 03 E8  XX
头      LEN=4  传感器 订阅  data_id   interval    CRC
```

**取消订阅：**
```
A5 5A  00 02  02  04  00 01  XX
头      LEN=2  传感器 取消  data_id  CRC
```

**错误响应（未知命令 0xFF）：**
```
A5 5A  00 01  FF  80  01  XX
头      LEN=1  请求  错误  未知命令  CRC
```

---

## 命令大类定义

| CMD | 名称 | 说明 |
|---|---|---|
| `0x01` | LED | LED 读写控制 |
| `0x02` | Sensor | 传感器读写 + 数据流订阅 |
| `0x03` | System | 系统信息、重启、日志等级调整 |

### LED (0x01) 子命令

| SUB | 操作 | PAYLOAD |
|---|---|---|
| `0x01` | 写 LED 状态 | `[led_id 1B] [state 1B]` state: 0=关 1=开 |
| `0x02` | 读 LED 状态 | `[led_id 1B]` |
| 响应 | 返回状态 | `[led_id 1B] [state 1B]` |

### Sensor (0x02) 子命令

| SUB | 操作 | PAYLOAD |
|---|---|---|
| `0x02` | 读一次 | 无（返回当前传感器值） |
| `0x03` | 订阅数据流 | `[data_id 2B] [interval_ms 2B]` |
| `0x04` | 取消订阅 | `[data_id 2B]` |
| 推送 | 主动推送 | `[data_id 2B] [value 4B float]` |

data_id 定义：`0x0001`=温度, `0x0002`=湿度

### System (0x03) 子命令

| SUB | 操作 | PAYLOAD |
|---|---|---|
| `0x02` | 查询系统信息 | 无，返回版本号、运行时间等 |
| `0x03` | 设置日志等级 | `[level 1B]` 0=DEBUG 1=INFO 2=WARN 3=ERROR |

---

## 错误码

响应帧 PAYLOAD 首字节为错误码：

| 错误码 | 含义 |
|---|---|
| `0x00` | 成功 |
| `0x01` | 未知命令大类 |
| `0x02` | 未知子命令 |
| `0x03` | 参数非法（长度/取值错误） |
| `0x04` | 硬件操作失败 |
| `0x05` | 资源忙（锁被占用超时） |

---

## 连接管理

### 连接结构 (cmd_conn_t)

| 字段 | 说明 |
|---|---|
| `fd` | socket 文件描述符 |
| `transport_type` | Unix Socket 或 TCP |
| `rx_buf` | 接收缓冲区（拼帧用，处理粘包） |
| `tx_queue` | 待发送帧队列（响应+推送） |
| `subs` | 该连接订阅的数据流 ID 集合 |
| `last_active` | 最后活跃时间戳（心跳/超时踢出用） |

### 生命周期

```
accept ─→ 注册 epoll EPOLLIN ─→ 收帧/发帧循环 ─→ close/超时
                       └─ 订阅/取消 ─┘
```

- **accept**：分配 `cmd_conn_t`，加入 epoll
- **收帧**：EPOLLIN → 追加 rx_buf → 尝试解析帧 → 交给调度层
- **发帧**：帧先入 tx_queue，注册 EPOLLOUT，可写时一次性 flush
- **超时**：定时器检查 last_active，超过 60s 无数据的连接自动关闭

---

## 订阅/推送机制

### 流程

1. 上位机发送订阅帧：`CMD=Sensor SUB=Subscribe PAYLOAD=[data_id, interval_ms]`
2. dispatcher 调用 subscription_mgr 为该 data_id 注册订阅者（conn, interval）
3. 传感器工作线程采集到新数据 → 调用 `cmd_subscription_push(data_id, value)`
4. subscription_mgr 遍历该 data_id 的订阅者列表 → 组推送帧 → 写入各 conn.tx_queue → 唤醒 epoll
5. 上位机取消订阅：`SUB=Unsubscribe PAYLOAD=[data_id]`

### 数据结构

```c
typedef struct {
    uint16_t data_id;           // 数据流 ID
    uint32_t interval_ms;       // 推送间隔（0 表示事件驱动，不定时）
    cmd_conn_t** subscribers;   // 订阅者连接列表
    int subscriber_count;
} cmd_subscription_t;
```

---

## 并发模型

采用**混合模式**：

- **主线程**：epoll 事件循环，处理所有网络 I/O（accept/read/write），微秒级响应
- **工作线程**：传感器定时采集，采集完成后调用 `cmd_subscription_push()` 触发推送
- **同步**：硬件共享资源通过 `hw_mutex` 保护；tx_queue 通过连接级锁保护

---

## 与 main.c 集成

原有的 `while(1)` 传感器循环迁移到独立线程。主线程交付给 `cmd_server_run()`（epoll 事件循环）：

```c
int main(int argc, char *argv[]) {
    atexit(cleanup);
    app_signal_init();
    log_init("/tmp/project.log", LOG_DEBUG);

    // 初始化硬件
    // 启动传感器采集线程

    // 启动命令服务（阻塞）
    cmd_server_t* server = cmd_server_create();
    cmd_server_listen_unix(server, "/tmp/cmd.sock");
    cmd_server_listen_tcp(server, 9527);
    cmd_server_run(server);  // 阻塞

    cmd_server_destroy(server);
    return 0;
}
```

---

## 模块目录结构

```
modules/cmd/
├── CMakeLists.txt
├── include/cmd/
│   ├── cmd_transport.h      # 传输层接口
│   ├── cmd_protocol.h       # 帧组包/拆包/CRC
│   ├── cmd_server.h         # epoll 服务 + 连接管理
│   ├── cmd_dispatcher.h     # 命令路由
│   ├── cmd_subscription.h   # 订阅管理
│   ├── cmd_frame.h          # 帧结构定义
│   ├── cmd_handler_led.h    # LED 命令处理器
│   └── cmd_handler_sensor.h # 传感器命令处理器
├── src/
│   ├── cmd_transport.c
│   ├── cmd_protocol.c
│   ├── cmd_server.c
│   ├── cmd_dispatcher.c
│   ├── cmd_subscription.c
│   ├── cmd_handler_led.c
│   └── cmd_handler_sensor.c
└── tests/
    └── test_cmd_protocol.c   # 协议层单元测试
```

---

## 接口摘要

| 层 | 核心接口 | 职责 |
|---|---|---|
| 传输层 | `transport_listen()`, `transport_accept()`, `transport_read()`, `transport_write()` | 监听/接受连接，收发字节 |
| 协议层 | `protocol_pack()`, `protocol_parse()`, `protocol_crc()` | 组帧/拆帧/校验 |
| 服务层 | `server_create()`, `server_listen_unix()`, `server_listen_tcp()`, `server_run()` | epoll 循环，连接生命周期 |
| 调度层 | `dispatcher_register()`, `dispatcher_dispatch()` | CMD→handler 路由 |
| 订阅 | `subscription_add()`, `subscription_remove()`, `subscription_push()` | 数据流订阅/推送 |
| 处理器 | `cmd_led_handler()`, `cmd_sensor_handler()`, `cmd_system_handler()` | 具体命令执行 |

---

## 后续扩展

- 固件升级（OTA）命令大类，可扩 LEN 或分片传输
- MQTT/蓝牙协议适配器，复用调度层和处理器
- 认证机制（连接建立后先握手）
- TLS 加密（TCP 场景）
