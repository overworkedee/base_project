# PC Dashboard 上位机指南

## 1. 定位

基于 PySide6 的跨平台桌面 GUI，通过 TCP 连接 Orange Pi 的 cmd 模块，实现开发板状态监控、控制、日志回显和 SSH 终端。

## 2. 三层架构

```
┌────────────────────────────────────────────────────┐
│  gui/                       展示 + 交互             │
│  ├─ main_window.py          主窗口（连接栏 + Tab）   │
│  ├─ dashboard_tab.py        仪表盘（温湿度 + LED）   │
│  ├─ log_tab.py              日志流（过滤 + 彩色）    │
│  ├─ system_tab.py           系统（版本 + 日志等级）  │
│  └─ terminal_tab.py         SSH 终端（paramiko）    │
│         ↑ Qt signals                               │
├────────────────────────────────────────────────────┤
│  models/                    数据缓存（解耦 GUI）     │
│  ├─ sensor_model.py         温湿度 + 订阅状态        │
│  ├─ led_model.py            LED 开关 + 防抖         │
│  ├─ system_model.py         版本 + 日志等级          │
│  └─ log_model.py            环形缓冲 + 等级过滤      │
│         ↑ 方法调用                                  │
├────────────────────────────────────────────────────┤
│  protocol/                  二进制协议 + TCP 通信    │
│  ├─ cmd_defs.py             协议常量（完整移植 C）   │
│  ├─ cmd_frame.py            帧打包/拆包 + CRC       │
│  └─ cmd_client.py           TCP 客户端 + 后台线程    │
│         ↓                                          │
│  TCP Socket ────► Orange Pi :9527                   │
└────────────────────────────────────────────────────┘
```

**分层原则**：
- **protocol/** 只做二进制帧转换和 socket 通信，不知道业务含义
- **models/** 缓存数据，通过 Qt signals 通知 GUI，不知道窗口长什么样
- **gui/** 只渲染和交互，不知道帧格式

## 3. 函数调用链与线程边界

以下从 main.py 启动开始，追踪每个操作的完整函数调用路径。

### 3.0 启动阶段

```
main.py                                      [主线程 Qt]
  QApplication()
  window = MainWindow()
    ├── CmdClient()                          protocol/cmd_client.py
    │     ├── _sock = None
    │     ├── _req_lock = Lock()
    │     ├── _sock_lock = Lock()
    │     └── _resp_event = Event()
    │
    ├── SensorModel(client)
    │     └── temperature_updated = Signal(float)    ← Qt 信号
    │         humidity_updated = Signal(float)
    │
    ├── LedModel(client)
    ├── SystemModel(client)
    ├── LogModel(client)
    │     └── buffer = RingBuffer(5000)              utils/ring_buffer.py
    │
    ├── TerminalTab()
    │     └── _SshWorker()                           内置 SSH 工作线程
    │
    ├── _setup_ui()
    │     ├── QLineEdit("192.168.3.171"), QLineEdit("9527")
    │     ├── QPushButton("Connect") → _on_connect
    │     ├── DashboardTab(sensor_model, led_model)  gui/dashboard_tab.py
    │     ├── LogTab(log_model)                      gui/log_tab.py
    │     ├── SystemTab(system_model)                gui/system_tab.py
    │     ├── TerminalTab()                          gui/terminal_tab.py
    │     └── QTabWidget 添加 4 个标签页
    │
    └── _connect_signals()
          ├── sensor_model.temperature_updated → _update_status_summary
          └── sensor_model.humidity_updated → _update_status_summary

  window.show()
  app.exec()                                ← 进入 Qt 事件循环（阻塞）
```

### 3.1 连接流程（Connect 按钮 → 数据就绪）

```
[主线程 Qt]
用户点击 Connect
  │
  ▼ main_window.py
  _on_connect()
    ├── host = self.host_input.text()       ← "192.168.3.171"
    ├── port = int(self.port_input.text())  ← 9527
    │
    └── self.client.connect(host, port)     protocol/cmd_client.py
          │                                  ┌──── 线程边界 ────┐
          ├── self.disconnect()             │ 主线程操作        │
          │     ├── _stop_reader()          │                   │
          │     └── sock.close()            │                   │
          │                                 │                   │
          ├── socket(AF_INET, SOCK_STREAM)  │                   │
          ├── sock.settimeout(3.0)          │                   │
          ├── sock.connect((host, port))    │                   │
          ├── sock.settimeout(None)         │                   │
          │                                 │                   │
          ├── _connected = True             │                   │
          │                                 ├───────────────────┤
          └── _start_reader()               │ 启动后台线程      │
                ├── _reader_running = True  │                   │
                └── Thread(target=_reader_loop, daemon=True)
                                            └───────────────────┘

  [回到主线程]
  self.on_connected() 依次调用所有 Model:

  ├── sensor_model.on_connected()           models/sensor_model.py
  │     ├── client.send_request(CMD_SENSOR, SUB_READ)        ← 读初始值
  │     │     └── (见 3.2 请求-响应链路)
  │     ├── 解析 temp/hum float BE → temperature_updated.emit()
  │     ├── client.subscribe(CMD_DATA_TEMPERATURE, 1000)     ← 订阅 1s
  │     │     └── send_request(CMD_SENSOR, SUB_SUBSCRIBE, [id|interval BE])
  │     └── client.subscribe(CMD_DATA_HUMIDITY, 1000)
  │
  ├── led_model.on_connected()
  │     └── refresh() → send_request(CMD_LED, SUB_READ, [led_id=1])
  │
  ├── system_model.on_connected()
  │     └── send_request(CMD_SYSTEM, SUB_READ) → version_updated.emit()
  │
  └── log_model.on_connected()
        └── client.subscribe(CMD_DATA_LOG, 0)  ← interval=0 事件驱动
              └── send_request(CMD_SYSTEM, CMD_SUB_LOG_SUBSCRIBE, [interval BE])
```

### 3.2 请求-响应链路（UI 操作 → 网络 → 响应 → GUI 更新）

以 LED ON 为例：

```
[主线程 Qt]
dashboard_tab._set_led(True)
  │
  ▼ models/led_model.py
  set_led(led_id=1, on=True)
    ├── 防抖检查: now - _last_action < 500ms?  → return False
    ├── pl = bytes([1, 0x01])                    ← [led_id, state]
    │
    └── self._client.send_request(CMD_LED, CMD_SUB_WRITE, pl)
          │                                       ┌── 串行请求锁 ──┐
          ├── _req_lock.acquire()                │                 │
          │                                       │                 │
          ├── frame = {cmd:0x01, sub:0x01,       │                 │
          │            payload: b"\x01\x01"}      │                 │
          ├── data = pack(frame)                  │ protocol/       │
          │     ├── struct.pack(">BBHB",          │ cmd_frame.py    │
          │     │     0xA5, 0x5A, 2, 0x01, 0x01) │  pack()         │
          │     ├── + b"\x01\x01"                 │                 │
          │     └── + bytes([crc8(all)])          │                 │
          │                                       │                 │
          ├── _resp_event.clear()                 │                 │
          ├── sock.sendall(data)                  │  TCP send       │
          │                                       │                 │
          ├── _resp_event.wait(3.0)               │  等待响应       │
          │     └── 阻塞主线程，最多 3 秒         │                 │
          │                                       │                 │
          ├── result = self._response             │                 │
          └── _req_lock.release()                └─────────────────┘

  [并行：后台线程]
  _reader_loop()                                [daemon 线程]
    sock.recv(4096) → buf.extend(chunk)
    │
    └── while True:                            ← 循环解析粘包
          frame, consumed = parse(buf)
          │                                     protocol/cmd_frame.py
          │   ├── 扫描 0xA5 0x5A               parse()
          │   ├── struct.unpack(">H", ...)      ← 读 LEN
          │   ├── crc8() 校验
          │   └── return {cmd, sub, len, payload}
          │
          if frame:
            _dispatch(frame)
              ├── if (sub & 0x80):              ← 响应帧
              │     if not _resp_event.is_set():
              │       _response = frame         ← 存入结果
              │       _resp_event.set()         ← 唤醒主线程!
              │     else:
              │       push_callback(frame)      ← 推送帧
              └── (请求帧忽略)

  [回到主线程，send_request 返回]
  if rsp["payload"][0] == CMD_ERR_OK:
      self.led_state[1] = True
      self.state_changed.emit(1, True)          ← Qt Signal
        │
        ▼ gui/dashboard_tab.py
        _on_led(1, True)
          self.led_state_label.setText("● ON")
          self.led_state_label.setStyleSheet("color: #27ae60; ...")
```

### 3.3 订阅推送链路（板端 → 后台线程 → 主线程 GUI）

```
[板端 sensor_thread]                          [Orange Pi]
  sht30_read_temperature(&temp_c)
  htonl(temp_c)
  cmd_subscription_push(sub_mgr, CMD_SENSOR, 0x0001, val, 4)
    → cmd_conn_send(conn, &push_frame) → write(fd) → TCP

  ════════════ 网络 ════════════

[后台 daemon 线程]                              [PC]
  _reader_loop():
    sock.recv(4096) → 收到推送帧
    parse(buf) → {cmd:0x02, sub:0x83, payload}
    _dispatch(frame):
      sub & 0x80 == True ← 响应帧
      _resp_event.is_set() == True ← 无请求等待
      → push_callback(frame)        ← 调用注册的回调
        │
        │ 即 MainWindow._on_push()     gui/main_window.py
        │                                ┌──── 跨线程 ────┐
        │   self.sensor_model.handle_push(frame)           │
        │   self.log_model.handle_push(frame)              │
        │                                                 │
        ▼ models/sensor_model.py                          │
        handle_push(frame):                               │
          ├── data_id = struct.unpack(">H", pl[0:2])      │
          ├── if data_id == CMD_DATA_TEMPERATURE:         │
          │     raw = struct.unpack(">I", pl[2:6])        │
          │     temp = struct.unpack("!f", pack(">I",raw))│
          │     self.temperature_updated.emit(temp) ←Signal│
          │                                                │
          ▼ gui/dashboard_tab.py           [自动跨线程到主线程]│
          _on_temp(28.3):                 ◄──────────────────┘
            self.temp_value.setText("28.3")
            if value > 40.0:
              self.temp_value.setStyleSheet(红色)
            self.temp_status.setText("● 订阅中")
```

### 3.4 SSH 终端链路

```
[主线程 Qt]
terminal_tab.py
用户输入命令 "ls /tmp"，按 Enter
  │
  ▼
  _on_send()
    cmd = self.cmd_input.text()
    self._worker.send_command(cmd + "\n")
      │
      └── self._channel.send("ls /tmp\n")    ← paramiko SSH channel

  ════════ SSH 加密通道 ════════

[后台 daemon 线程]
  _SshWorker._connect_thread():
    while _running:
      chunk = self._channel.recv(4096)       ← 阻塞读取
      buf.extend(chunk)
      text = buf.decode('utf-8')
      clean = _strip_ansi(text)             ← 过滤 ANSI 转义
      self.output_received.emit(clean)      ← Qt Signal
        │                                     ┌── 跨线程 ──┐
        ▼ gui/terminal_tab.py                │             │
        _on_output(text):                    │             │
          self.terminal.moveCursor(End)      │             │
          self.terminal.insertPlainText(text)│             │
          scrollbar.setValue(maximum)  ◄─────┘ 自动回主线程 │
                                                           │
[断开]                                                    │
  _on_disconnect():                                       │
    self._worker.disconnect()                             │
      ├── _channel.close()                                │
      ├── _client.close()                                 │
      └── connection_changed.emit(False)  ─────────────────┘
          → status_label.setText("⚫ 未连接")
```

### 3.5 线程安全边界总览

```
┌─ 主线程 (Qt Event Loop) ──────────────────────────────────────────┐
│                                                                   │
│  GUI Widgets: 所有 QWidget 操作（setText/setStyleSheet/insertRow）│
│  Models:      所有 emit() Qt Signal 调用                          │
│  CmdClient:   connect() / disconnect() / send_request()           │
│               send_request() 内部 _req_lock 串行化                │
│                                                                   │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  后台 daemon 线程 (_reader_loop)                                   │
│  ├── sock.recv(4096)          ← 阻塞读取 TCP                      │
│  ├── parse(buf)               ← 帧解析（无锁，只读局部 buf）      │
│  ├── _dispatch(frame)         ← 分发响应/推送                     │
│  │     ├── _resp_event.set()  ← 唤醒主线程 send_request           │
│  │     └── push_callback()    ← 调用 Qt Signal → 自动跨线程       │
│  └── _on_connection_lost()    ← 检测连接断开                      │
│                                                                   │
│  后台 daemon 线程 (SSH _connect_thread)                            │
│  ├── channel.recv(4096)       ← 阻塞读取 SSH channel              │
│  ├── _strip_ansi()            ← 过滤 ANSI 转义                    │
│  └── output_received.emit()   ← Qt Signal → 自动跨线程到 GUI      │
│                                                                   │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  跨线程安全机制:                                                   │
│    Qt Signal/Slot 自动排队到主线程（默认 Qt::AutoConnection）      │
│    _req_lock (threading.Lock) — 串行化 send_request               │
│    _sock_lock (threading.Lock) — 保护 _sock 的读写                │
│    RingBuffer._lock — 保护环形缓冲区                              │
│    _resp_event (threading.Event) — 请求-响应的跨线程同步          │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

## 4. 数据流（简化版）

### 4.1 请求-响应（以 LED 开关为例）

```
用户点击 ON 按钮
  │
  ▼ dashboard_tab.py
  self._led.set_led(led_id=1, on=True)
  │
  ▼ led_model.py
  pl = bytes([1, 0x01])     ← [led_id 1B, state 1B]
  rsp = self._client.send_request(CMD_LED, CMD_SUB_WRITE, pl)
  │
  ▼ cmd_client.py
  frame = {"cmd": 0x01, "sub": 0x01, "payload": b"\x01\x01"}
  data = pack(frame)        ← 序列化为二进制帧（含 CRC）
  self._sock.sendall(data)
  │
  │── 等待响应（_resp_event.wait(3s)）──
  │
  ▼ _reader_loop 后台线程收帧
  chunk = sock.recv(4096)   ← 阻塞读取
  frame, consumed = parse(buf)  ← 解析帧
  _dispatch(frame)
    → _resp_event.set()     ← 唤醒 send_request 的等待线程
  │
  ▼ led_model.py
  if rsp["payload"][0] == CMD_ERR_OK:
      self.led_state[1] = True
      self.state_changed.emit(1, True)   ← Qt Signal
  │
  ▼ dashboard_tab.py
  _on_led(1, True)
    self.led_state_label.setText("● ON")  ← 刷新 GUI
```

### 4.2 订阅推送（以温度为例）

```
连接建立后:
  sensor_model.on_connected()
    → client.send_request(CMD_SENSOR, CMD_SUB_READ)  ← 读初始值
    → client.subscribe(CMD_DATA_TEMPERATURE, 1000)   ← 订阅 1 秒推送

板端 sensor_thread 每秒 push:
  推送帧 {cmd:0x02, sub:0x83, payload:[0x0001 BE, 28.3f BE]}

cmd_client._reader_loop 收帧:
  _dispatch(frame)
    → _resp_event 已设置（无请求等待中）
    → push_callback(frame)  ← 回调 MainWindow._on_push

MainWindow._on_push(frame):
  self.sensor_model.handle_push(frame)
    → data_id == CMD_DATA_TEMPERATURE
    → ntohl → float 转换
    → self.temperature_updated.emit(28.3)  ← Qt Signal

dashboard_tab._on_temp(28.3):
  self.temp_value.setText("28.3")  ← 实时刷新
```

### 4.3 日志推送

```
连接建立后:
  log_model.on_connected()
    → client.subscribe(CMD_DATA_LOG, 0)  ← interval=0 事件驱动

板端: 每次 LOG_* 宏 → on_log_push() → cmd_subscription_push(CMD_SYSTEM, CMD_DATA_LOG, ...)

推送帧 {cmd:0x03, sub:0x85, payload:[data_id BE | level | reserved | ts LE | msg]}

log_model.handle_push(frame):
  → 解析 level / timestamp / msg
  → buffer.push((level, ts_str, msg))    ← 环形缓冲区
  → log_received.emit(level, ts, msg)    ← Qt Signal

log_tab._on_log():
  → 插入 QTableWidget 新行
  → 按 level 着色（DEBUG 灰 / INFO 蓝 / WARN 橙 / ERROR 红）
  → 自动滚动到底部
```

## 5. 关键类设计

### 5.1 CmdClient — TCP 客户端

```
class CmdClient:
    # 线程模型：主线程调用 send_request，后台线程 recv + 解析
    _sock: socket            ← TCP 连接
    _sock_lock: Lock          ← 保护 _sock 的读写
    _req_lock: Lock          ← 串行化请求（一次只许一个请求飞行中）
    _resp_event: Event       ← 等待响应
    _response: dict          ← 当前响应结果
    _push_callback: Callable ← 推送帧回调（Qt Signal → MainWindow._on_push）

    send_request(cmd, sub, payload) → dict|None:
        1. _req_lock 加锁（串行）
        2. pack() 序列化
        3. sock.sendall() 发送
        4. _resp_event.wait(3s) 等响应
        5. 返回 _response
        6. _req_lock 释放

    _reader_loop():
        循环:
          sock.recv(4096) → 追加到 buf
          循环:
            parse(buf) → 解析帧
            若帧完整:
              if _resp_event 未设置 → emplace _response, set event（响应帧）
              else → push_callback(frame)（推送帧）
            若 CRC 失败 → 跳过 1B 继续
            若数据不足 → 退出内循环等更多数据
```

**为什么不用 request_id 匹配？** 板端 handler 直接解析 payload（如 `[led_id, state]`），如果 payload 首字节插入 `request_id`，handler 会误读。串行请求避免了这个问题，且 GUI 操作天然串行。

### 5.2 数据模型 — Qt 信号驱动

所有 Model 继承 `QObject`，通过 Qt Signal 通知 GUI 更新。Model 持有 `CmdClient` 引用，负责构造正确的 payload 发送请求。

```python
class SensorModel(QObject):
    temperature_updated = Signal(float)     # GUI 绑定此信号
    humidity_updated = Signal(float)
    subscription_changed = Signal(int, bool)

    def on_connected(self):                # 连接成功后 MainWindow 调用
        self._client.send_request(...)     # 读初始值 + 订阅推送

    def handle_push(self, frame):          # MainWindow._on_push 分发
        解析推送帧 → emit signal
```

### 5.3 RingBuffer — 线程安全环形缓冲

```python
class RingBuffer(capacity):
    _buf: list[capacity]     # 固定大小数组
    _head: int               # 写入位置
    _count: int              # 当前元素数
    _lock: threading.Lock

    push(item):
        写 _buf[_head]，_head 前移，满了覆盖最旧

    get_all():
        从旧到新返回所有元素，不修改缓冲区
```

日志 Tab 用 5000 条容量，满了自动覆盖。

## 6. Tab 页面说明

| Tab | 数据来源 | 交互 |
|-----|---------|------|
| **仪表盘** | SensorModel 推送 | 温湿度大字体卡片（>40°C 红色，<30% 黄色），LED 开关（500ms 防抖） |
| **日志** | LogModel 推送 | QTableWidget 三列（时间/等级/消息），INFO 蓝/WARN 橙/ERROR 红，等级过滤下拉，暂停/继续，清空 |
| **系统** | SystemModel 响应 | 版本显示，日志等级 Radio 按钮 + 应用 |
| **终端** | paramiko SSH | 独立于 cmd 协议，暗色背景+等宽字体，命令输入+发送，ANSI 转义过滤 |

## 7. 扩展新功能

### 添加新的监控项（如 CPU 温度）

1. **板端**：按 [cmd/GUIDE.md](../modules/cmd/GUIDE.md#7-扩展新命令) 添加 handler
2. **PC 端 protocol/cmd_defs.py**：加 `CMD_CPU = 0x04` 等常量
3. **PC 端 models/**：新建 `cpu_model.py`，继承 QObject，实现 `on_connected()` + `handle_push()`
4. **PC 端 gui/**：新建 `cpu_tab.py` 或扩展现有 Tab
5. **PC 端 gui/main_window.py**：实例化新 Model + 添加到 Tab
6. **PC 端 models/__init__.py** 和 **gui/__init__.py**：无需修改

**原则：新增不改旧代码。**

## 8. 关键设计决策

| 决策 | 原因 |
|------|------|
| TCP（不用 Unix Socket） | 跨平台（Windows 无 Unix Socket） |
| 串行请求（不用 request_id） | 避免污染 payload 导致 handler 误读 |
| 后台线程收帧 + 主线程 GUI | Qt 要求 widget 操作必须在主线程，Signal 自动跨线程 |
| Model 层解耦 | GUI 换框架（如换 Tkinter）只需重写 gui/，protocol/ 和 models/ 不变 |
| 终端用 paramiko（不用 subprocess ssh） | 跨平台一致，不需要系统安装 ssh 命令 |
| Fusion 样式 | 跨平台外观统一，Windows/Linux 一个样 |
| log 同时写 cmd 环形缓冲 + log 文件 | 互补：cmd 供上位机实时回显，文件持久化保存 |

## 9. 运行

```bash
cd ~/base_project
python3 pc_dashboard/main.py
```

或模块方式：

```bash
python3 -m pc_dashboard.main
```

依赖：`pip install PySide6 paramiko`
