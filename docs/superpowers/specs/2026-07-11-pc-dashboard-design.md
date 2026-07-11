# PC 上位机仪表盘 — 设计文档

**日期:** 2026-07-11  
**状态:** Draft  
**目标:** 使用 PySide6 构建跨平台 PC 桌面 GUI，通过 cmd 模块二进制协议与 Orange Pi 5 Plus 开发板通信，实现状态监控 + 控制 + 日志回显。

---

## 1. 动机

现有 cmd_demo（终端交互式客户端）功能完备但不够直观：无法同时查看温湿度趋势、LED 状态和日志流。需要一个图形化上位机，替代命令行操作，且可在 Windows/Linux 双平台运行。

---

## 2. 架构总览

```
┌─ PC 上位机 (Python/PySide6) ────────────────────────────────┐
│                                                              │
│  gui/ (Widgets)          # 展示 + 交互                       │
│    └─ main_window.py     # 主窗口，Tab 容器                   │
│    └─ dashboard_tab.py   # 仪表盘: 温湿度卡片 + LED 控制      │
│    └─ log_tab.py         # 滚动日志流 + 等级过滤              │
│    └─ system_tab.py      # 系统信息 + 日志等级控制             │
│       ↓ Qt signals/slots                                      │
│  models/ (DataModel)     # 数据缓存，解耦 GUI 与协议          │
│    └─ sensor_model.py    # 温湿度 + 订阅状态                  │
│    └─ led_model.py       # LED 开/关状态                      │
│    └─ system_model.py    # 版本字符串                         │
│    └─ log_model.py       # 环形缓冲日志                       │
│       ↓                                                       │
│  protocol/               # 二进制帧打包/拆包 + socket 通信    │
│    └─ cmd_client.py      # 连接管理、收发帧、订阅线程          │
│    └─ cmd_frame.py       # pack()/parse() Python 实现          │
│    └─ cmd_defs.py        # 协议常量（CMD_LED 等）              │
│       ↓                                                       │
│  TCP Socket ─────────── 网络 ──────────► Orange Pi :9527       │
└──────────────────────────────────────────────────────────────┘
```

分层原则：协议层只做二进制帧转换；模型层缓存数据、通过 Qt signals 通知 GUI；GUI 层只做渲染。扩展新功能只需新增一个 Model + 一个 Tab Widget，不动现有层级。

---

## 3. 协议层

### 3.1 `cmd_defs.py` — 协议常量

Python 版本的 cmd_frame.h。完整移植所有宏：

```python
# 帧定义
CMD_FRAME_HEAD_MAGIC  = 0xA5
CMD_FRAME_TAIL_MAGIC  = 0x5A
CMD_FRAME_HEADER_SIZE = 6
CMD_SUB_RESPONSE_FLAG = 0x80

# 命令大类
CMD_LED    = 0x01
CMD_SENSOR = 0x02
CMD_SYSTEM = 0x03

# 子命令
CMD_SUB_WRITE                = 0x01
CMD_SUB_READ                 = 0x02
CMD_SUB_SUBSCRIBE            = 0x03
CMD_SUB_UNSUBSCRIBE          = 0x04
CMD_SUB_LOG_SUBSCRIBE        = 0x05  # 新增
CMD_SUB_LOG_UNSUBSCRIBE      = 0x06  # 新增

# 数据流 ID
CMD_DATA_TEMPERATURE  = 0x0001
CMD_DATA_HUMIDITY     = 0x0002
CMD_DATA_LOG          = 0x0003  # 新增

# 错误码
CMD_ERR_OK          = 0x00
CMD_ERR_UNKNOWN_CMD = 0x01
CMD_ERR_UNKNOWN_SUB = 0x02
CMD_ERR_PARAM       = 0x03
CMD_ERR_HARDWARE    = 0x04
CMD_ERR_BUSY        = 0x05
```

### 3.2 `cmd_frame.py` — 帧打包/拆包

```python
def pack(frame: dict) -> bytes:
def parse(data: bytes) -> tuple[dict | None, int]:
def sub_rsp(sub: int) -> int:
def sub_req(sub: int) -> int:
def crc8(data: bytes) -> int:
```

`pack()` 将 dict（含 cmd/sub/len/payload 字段）序列化为二进制帧（含 CRC）。
`parse()` 从字节流中扫描帧头 0xA5 0x5A，解析完整帧，返回 (frame_dict, consumed_bytes)。

### 3.3 `cmd_client.py` — 连接与通信

```python
class CmdClient:
    def connect(host: str, port: int) -> bool
    def disconnect()
    def send_request(cmd, sub, payload=bytes) -> CmdResponse
    def subscribe(data_id, interval_ms) -> bool
    def unsubscribe(data_id) -> bool
    def on_push(callback)             # 注册推送回调（订阅数据 + 日志）
    def is_connected() -> bool
```

**线程模型：**
- 主线程：GUI 事件循环，所有 Qt widget 操作
- 后台线程：阻塞 socket read，解析帧。响应帧匹配 request_id 后通过 callback 返回；推送帧（bit7=1 且非 rsp）调用 `on_push` 回调

**帧匹配：** 对请求-响应帧，在 paylod 首字节加 1B request_id，递增滚动，响应帧也含相同 request_id。这样并发请求不会串包。订阅推送不需要 request_id。

**连接管理：**
- connect 失败返回 False，GUI 显示连接错误
- 后台线程检测到 read()==0（对端关闭），通过 signal 通知 GUI 断开
- 支持手动重连，重连后自动恢复所有订阅

---

## 4. 数据模型层

每个 Model 持有业务数据，通过 Qt QObject signals 通知 GUI 更新。

### 4.1 `SensorModel` (QObject)

```python
class SensorModel(QObject):
    # signals
    temperature_updated = Signal(float)
    humidity_updated = Signal(float)
    subscription_changed = Signal(int, bool)  # data_id, is_subscribed

    # 数据
    temperature: float
    humidity: float
    subscribed: set[int]  # {CMD_DATA_TEMPERATURE, CMD_DATA_HUMIDITY}
```

- 连接成功后自动发送 `CMD_SENSOR + SUB_READ` 获取初始值
- 自动订阅温湿度（interval=1000ms），收到推送时更新值并 emit signal
- 断开时清除订阅标记

### 4.2 `LedModel` (QObject)

```python
class LedModel(QObject):
    # signals
    state_changed = Signal(int, bool)  # led_id, is_on

    # 数据
    led_state: dict[int, bool]  # led_id -> is_on
```

- `set_led(led_id, on)` — 发送 `CMD_LED + SUB_WRITE`
- `refresh()` — 发送 `CMD_LED + SUB_READ`，适用于连接后恢复状态
- 连续操作防抖：500ms 内重复操作忽略

### 4.3 `SystemModel` (QObject)

```python
class SystemModel(QObject):
    # signals
    version_updated = Signal(str)
    loglevel_updated = Signal(int)
    connected = Signal(bool)

    # 数据
    version: str
    log_level: int  # 0=DEBUG 1=INFO 2=WARN 3=ERROR
```

- 连接成功后自动查询系统版本
- `set_log_level(level: int)` — 发送 `CMD_SYSTEM + sub=0x03`

### 4.4 `LogModel` (QObject)

```python
class LogModel(QObject):
    # signals
    log_received = Signal(int, str)  # level, message
    log_cleared = Signal()

    # 数据
    buffer: RingBuffer(5000)  # 线程安全环形缓冲区
    subscribed: bool
    filter_level: int  # 日志等级过滤，只显示 >= filter_level 的日志
```

- 连接成功后自动发送 `CMD_SYSTEM + CMD_SUB_LOG_SUBSCRIBE` 订阅日志
- 收到推送帧后解析 `[level:1B, reserved:1B, timestamp:4B LE, msg:N B]`，存入环形缓冲
- 支持 `set_filter(level)` 过滤展示
- 支持 `clear()` 清空缓冲区

---

## 5. GUI 层

### 5.1 `MainWindow` — 主窗口

```
┌─────────────────────────────────────────────────┐
│  PC Dashboard                    192.168.3.171  │
│  [○ Connected] [⚡ Reconnect]                    │
├─────────────────────────────────────────────────┤
│  ┌─ 仪表盘 ──┬── 日志 ──┬── 系统 ──┐            │
│  │           │           │          │            │
│  │           │           │          │            │
│  └───────────┴───────────┴──────────┘            │
├─────────────────────────────────────────────────┤
│  Status: Connected | 温度: 28°C | 湿度: 65%     │
└─────────────────────────────────────────────────┘
```

- 顶部连接栏：IP/端口输入 + 连接状态指示（绿/红圆点）+ 重连按钮
- QTabWidget 三个标签页：仪表盘、日志、系统
- 底部状态栏：连接状态 + 关键数据摘要

**窗口关闭时自动断开连接、取消所有订阅。**

### 5.2 `DashboardTab` — 仪表盘页

```
┌──────────────────────────────────────────┐
│                                          │
│   ┌───────────┐   ┌───────────┐          │
│   │  温度 °C   │   │  湿度 %RH  │          │
│   │           │   │           │          │
│   │   28.3    │   │   65.2    │          │
│   │  ● 订阅中 │   │  ● 订阅中 │          │
│   └───────────┘   └───────────┘          │
│                                          │
│   ┌─ LED 控制 ──────────────────────┐     │
│   │  blue_led  [○ OFF] [✗ ON]       │     │
│   │  当前状态: ● ON                  │     │
│   └────────────────────────────────┘     │
│                                          │
└──────────────────────────────────────────┘
```

- 温湿度大字体卡片，颜色根据数值变化（温度>40°C 红色，湿度<30% 黄色）
- 订阅指示灯：绿色=已订阅，灰色=未订阅
- LED 开关带确认反馈（发送后按钮短暂禁用 500ms）

### 5.3 `LogTab` — 日志流

```
┌──────────────────────────────────────────┐
│  等级过滤: [ALL ▼] [清空] [暂停 ⏸]        │
├──────────────────────────────────────────┤
│  12:30:01  INFO   SHT30 sensor init OK  │
│  12:30:02  DEBUG  Temperature 28.3°C    │
│  12:30:03  WARN   LED brightness low    │
│  12:30:04  ERROR  malloc failed         │
│  12:30:05  INFO   Client connected      │
│  ...                                     │
├──────────────────────────────────────────┤
│  共 1280 条，已显示 1280 条                │
└──────────────────────────────────────────┘
```

- QTableWidget 三列：时间（格式化自 timestamp）、等级（彩色标签）、消息
- INFO 蓝色、WARN 橙色、ERROR 红色、DEBUG 灰色
- 等级过滤下拉框（ALL / INFO+ / WARN+ / ERROR only）
- 暂停按钮：暂停时日志继续缓存但停止滚动，恢复后追上新日志
- 自动滚动到底部，用户手动上滚时暂停自动滚动

### 5.4 `SystemTab` — 系统页

```
┌──────────────────────────────────────────┐
│  系统信息                                 │
│  ├─ 固件版本:  1.0.0                      │
│  ├─ 连接方式:  TCP (192.168.3.171:9527)   │
│  └─ 连接状态:  ● 已连接                   │
│                                          │
│  日志等级                                 │
│  ├─ ○ DEBUG  ○ INFO  ● WARN  ○ ERROR     │
│  └─  [应用]                               │
│                                          │
│  统计                                     │
│  ├─ 仪表盘刷新率: 10Hz                    │
│  ├─ 日志接收: 1280 条                     │
│  └─ 连接时长: 0h 15m 32s                  │
└──────────────────────────────────────────┘
```

- QRadioButton 切换日志等级，点应用后通过 SystemModel 发送命令
- 连接统计实时更新

---

## 6. 板端改动

### 6.1 log 模块 (`modules/log/log.c`)

- 新增环形缓冲区（`LOG_RING_SIZE=2048` 条），每条为固定大小结构体：

  ```c
  typedef struct {
      uint8_t  level;
      uint8_t  reserved;
      uint32_t timestamp;     /* unix timestamp，LE */
      char     msg[256];      /* 截断到 256 字节 */
  } log_ring_entry_t;
  ```

- 新增回调类型 `typedef void (*log_subscribe_fn_t)(uint8_t level, const char* msg, void* ctx);`
- 新增 `log_set_subscribe_callback(log_subscribe_fn_t cb, void* ctx)` — 设置订阅回调
- `LOG_INFO/WARN/ERROR/DEBUG` 宏内部：写文件后，写入环形缓冲，再调用回调推送

### 6.2 `cmd_frame.h` 追加定义

```c
#define CMD_DATA_LOG              0x0003
#define CMD_SUB_LOG_SUBSCRIBE     0x05
#define CMD_SUB_LOG_UNSUBSCRIBE   0x06
```

### 6.3 `cmd_handler_system.c` 追加操作

- `CMD_SUB_LOG_SUBSCRIBE (0x05)`: 在订阅管理器中注册 `CMD_DATA_LOG`，从 log 模块的环形缓冲区捞出已缓存的日志逐条推送（最多 500 条），然后开启实时推送
- `CMD_SUB_LOG_UNSUBSCRIBE (0x06)`: 取消日志订阅

### 6.4 `user/main.c`

- `cmd_handler_system` 当前 ctx 为 NULL（不需要硬件上下文），但日志订阅需要 `sub_mgr`。改为传递 `app_cmd_get_sub_mgr(g_cmd)` 作为 ctx
- `log_init` 之后调用 `log_set_subscribe_callback(on_log_push, app_cmd_get_sub_mgr(g_cmd))`
- 新增 `on_log_push(level, msg, ctx)` 回调：构造 `CMD_DATA_LOG` 帧数据，调用 `cmd_subscription_push(ctx, CMD_SYSTEM, CMD_DATA_LOG, data, len)`

---

## 7. 日志推送帧格式

```
CMD = CMD_SYSTEM
SUB = CMD_SUB_LOG_SUBSCRIBE | CMD_SUB_RESPONSE_FLAG
PAYLOAD = [
    level:1B         # LOG_DEBUG=0, LOG_INFO=1, LOG_WARN=2, LOG_ERROR=3
    reserved:1B      # 预留，填 0
    timestamp:4B     # unix timestamp，小端序 LE
    message:N B      # UTF-8 日志消息（含文件/行号/函数前缀）
]
```

推送帧复用现有的 `cmd_subscription_push()` 机制，`cmd=SENSOR` 改为 `cmd=SYSTEM`，`data_id=CMD_DATA_LOG`。

---

## 8. 错误处理

| 场景 | 处理 |
|------|------|
| 连接失败 | 禁用所有操作按钮，底部状态栏显示 "连接失败"，重连按钮激活 |
| 连接断开 | 通过后台线程 read()==0 检测，清除订阅标记，GUI 禁用操作，状态栏变红 |
| 帧解析失败 | 跳帧策略，不影响后续帧，LogModel 记录解析错误计数 |
| 请求超时 | 3s 超时定时器，超时显示 "无响应" 提示，不阻塞 GUI |
| 硬件错误 | 响应 error code ≠ OK，状态栏红色显示错误码字符串 + 3s 后恢复 |
| 重复连接 | connect 前自动 disconnect |

---

## 9. 测试策略

### 单元测试

- `cmd_frame.py` 帧打包/拆包：往返测试、空 payload、CRC 错误、分片、大 payload（64K 边界）
- `cmd_client.py`：mock socket，验证 send_request 帧格式、并发请求 request_id 匹配、推送帧分发
- `RingBuffer`：插入/读取/满时覆盖/线程安全

### 集成测试

- 启动 cmd_server（host 模式，mock handler），上位机连接并执行 send_request 往返
- 订阅推送测试：模拟 sensor_thread 定期 push，验证上位机收到并更新 model
- 日志订阅测试：模拟 log 缓冲 push，验证上位机 LogModel 正确接收

### 跨平台验证

- Linux (Ubuntu 22.04): 主开发平台
- Windows 10/11: 用 PySide6 相同版本号验证打包 exe

---

## 10. 项目文件结构

```
pc_dashboard/
├── main.py                    # 入口
├── requirements.txt           # PySide6
├── gui/
│   ├── __init__.py
│   ├── main_window.py
│   ├── dashboard_tab.py
│   ├── log_tab.py
│   └── system_tab.py
├── models/
│   ├── __init__.py
│   ├── sensor_model.py
│   ├── led_model.py
│   ├── system_model.py
│   └── log_model.py
├── protocol/
│   ├── __init__.py
│   ├── cmd_client.py
│   ├── cmd_frame.py
│   └── cmd_defs.py
└── utils/
    ├── __init__.py
    └── ring_buffer.py
```

---

## 11. 依赖

```
# requirements.txt
PySide6>=6.5.0
```

无其他依赖。TCP socket 用 Python 标准库 `socket`/`struct`/`threading`。
