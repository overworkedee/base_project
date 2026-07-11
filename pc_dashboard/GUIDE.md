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

## 3. 数据流

### 3.1 请求-响应（以 LED 开关为例）

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

### 3.2 订阅推送（以温度为例）

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

### 3.3 日志推送

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

## 4. 关键类设计

### 4.1 CmdClient — TCP 客户端

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

### 4.2 数据模型 — Qt 信号驱动

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

### 4.3 RingBuffer — 线程安全环形缓冲

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

## 5. Tab 页面说明

| Tab | 数据来源 | 交互 |
|-----|---------|------|
| **仪表盘** | SensorModel 推送 | 温湿度大字体卡片（>40°C 红色，<30% 黄色），LED 开关（500ms 防抖） |
| **日志** | LogModel 推送 | QTableWidget 三列（时间/等级/消息），INFO 蓝/WARN 橙/ERROR 红，等级过滤下拉，暂停/继续，清空 |
| **系统** | SystemModel 响应 | 版本显示，日志等级 Radio 按钮 + 应用 |
| **终端** | paramiko SSH | 独立于 cmd 协议，暗色背景+等宽字体，命令输入+发送，ANSI 转义过滤 |

## 6. 扩展新功能

### 添加新的监控项（如 CPU 温度）

1. **板端**：按 [cmd/GUIDE.md](../modules/cmd/GUIDE.md#7-扩展新命令) 添加 handler
2. **PC 端 protocol/cmd_defs.py**：加 `CMD_CPU = 0x04` 等常量
3. **PC 端 models/**：新建 `cpu_model.py`，继承 QObject，实现 `on_connected()` + `handle_push()`
4. **PC 端 gui/**：新建 `cpu_tab.py` 或扩展现有 Tab
5. **PC 端 gui/main_window.py**：实例化新 Model + 添加到 Tab
6. **PC 端 models/__init__.py** 和 **gui/__init__.py**：无需修改

**原则：新增不改旧代码。**

## 7. 关键设计决策

| 决策 | 原因 |
|------|------|
| TCP（不用 Unix Socket） | 跨平台（Windows 无 Unix Socket） |
| 串行请求（不用 request_id） | 避免污染 payload 导致 handler 误读 |
| 后台线程收帧 + 主线程 GUI | Qt 要求 widget 操作必须在主线程，Signal 自动跨线程 |
| Model 层解耦 | GUI 换框架（如换 Tkinter）只需重写 gui/，protocol/ 和 models/ 不变 |
| 终端用 paramiko（不用 subprocess ssh） | 跨平台一致，不需要系统安装 ssh 命令 |
| Fusion 样式 | 跨平台外观统一，Windows/Linux 一个样 |
| log 同时写 cmd 环形缓冲 + log 文件 | 互补：cmd 供上位机实时回显，文件持久化保存 |

## 8. 运行

```bash
cd ~/base_project
python3 pc_dashboard/main.py
```

或模块方式：

```bash
python3 -m pc_dashboard.main
```

依赖：`pip install PySide6 paramiko`
