# PC Dashboard Implementation Plan

> **For reviewers:** Steps use checkbox (`- [ ]`) syntax for progress tracking.

**Goal:** Build a PySide6 cross-platform desktop GUI that connects to Orange Pi 5 Plus via TCP on the cmd binary protocol, displaying sensor data / LED control / log stream / system info.

**Architecture:** Three-layer Python app (protocol → models → GUI) on the PC side, plus ring-buffer + log subscription hooks on the board side. Protocol layer handles binary frame pack/unpack and TCP socket with background reader thread. Models cache state and emit Qt signals. GUI renders tabbed dashboard with real-time updates.

**Tech Stack:** Python 3, PySide6 ≥ 6.5.0, C (board-side log/cmd modules), binary frame protocol over TCP

## Global Constraints

- All Python code goes under `pc_dashboard/`
- Board C changes go in existing `modules/log/`, `modules/cmd/`, `user/` directories
- TCP only (no Unix socket) for cross-platform compatibility
- Frame protocol matches existing cmd binary format exactly
- Log messages in C must be English; Python comments in Chinese
- All board-side changes must build with `./build.sh`

---

## Phase 1: PC Dashboard Foundation (Python)

### Task 1: Project Scaffold

**Files:**
- Create: `pc_dashboard/requirements.txt`
- Create: `pc_dashboard/protocol/__init__.py`
- Create: `pc_dashboard/protocol/cmd_defs.py`
- Create: `pc_dashboard/gui/__init__.py`
- Create: `pc_dashboard/models/__init__.py`
- Create: `pc_dashboard/utils/__init__.py`

**Interfaces:**
- Produces: `cmd_defs.py` with all protocol constants used by every downstream module

- [ ] **Step 1: Create directory structure**

```bash
mkdir -p pc_dashboard/{protocol,gui,models,utils}
```

- [ ] **Step 2: Write `pc_dashboard/requirements.txt`**

```
PySide6>=6.5.0
```

- [ ] **Step 3: Create `__init__.py` files** (empty) for all 4 packages

```bash
touch pc_dashboard/protocol/__init__.py
touch pc_dashboard/gui/__init__.py
touch pc_dashboard/models/__init__.py
touch pc_dashboard/utils/__init__.py
```

- [ ] **Step 4: Write `pc_dashboard/protocol/cmd_defs.py`**

```python
""" 命令帧协议常量 —— 完整移植自 modules/cmd/include/cmd/cmd_frame.h """

# 帧定义
CMD_FRAME_HEAD_MAGIC  = 0xA5
CMD_FRAME_TAIL_MAGIC  = 0x5A
CMD_FRAME_HEADER_SIZE = 6
CMD_FRAME_CRC_SIZE    = 1
CMD_FRAME_MAX_PAYLOAD = 65535  # 最大负载 64KB
CMD_SUB_RESPONSE_FLAG = 0x80

# 命令大类
CMD_LED    = 0x01
CMD_SENSOR = 0x02
CMD_SYSTEM = 0x03

# 子命令操作码
CMD_SUB_WRITE           = 0x01
CMD_SUB_READ            = 0x02
CMD_SUB_SUBSCRIBE       = 0x03
CMD_SUB_UNSUBSCRIBE     = 0x04
CMD_SUB_LOG_SUBSCRIBE   = 0x05
CMD_SUB_LOG_UNSUBSCRIBE = 0x06
CMD_SUB_SET_LOGLEVEL    = 0x03  # system handler 日志等级（内部编码）

# 数据流 ID
CMD_DATA_TEMPERATURE = 0x0001
CMD_DATA_HUMIDITY    = 0x0002
CMD_DATA_LOG         = 0x0003

# 错误码
CMD_ERR_OK          = 0x00
CMD_ERR_UNKNOWN_CMD = 0x01
CMD_ERR_UNKNOWN_SUB = 0x02
CMD_ERR_PARAM       = 0x03
CMD_ERR_HARDWARE    = 0x04
CMD_ERR_BUSY        = 0x05

# 日志等级（对应 log_level_t）
LOG_LEVEL_DEBUG = 0
LOG_LEVEL_INFO  = 1
LOG_LEVEL_WARN  = 2
LOG_LEVEL_ERROR = 3

_ERROR_STRINGS = {
    CMD_ERR_OK:          "OK",
    CMD_ERR_UNKNOWN_CMD: "Unknown command",
    CMD_ERR_UNKNOWN_SUB: "Unknown sub-command",
    CMD_ERR_PARAM:       "Invalid parameter",
    CMD_ERR_HARDWARE:    "Hardware error",
    CMD_ERR_BUSY:        "Resource busy",
}

def err_str(code: int) -> str:
    """ 错误码转可读字符串 """
    return _ERROR_STRINGS.get(code, f"Unknown({code})")
```

- [ ] **Step 5: Verify**

```bash
python3 -c "from pc_dashboard.protocol.cmd_defs import CMD_LED, CMD_SENSOR; print(f'CMD_LED={CMD_LED}, CMD_SENSOR={CMD_SENSOR}')"
```

- [ ] **Step 6: Commit**

```bash
git add pc_dashboard/
git commit -m "feat(pc_dashboard): add project scaffold and protocol constants"
```

---

### Task 2: Thread-Safe Ring Buffer

**Files:**
- Create: `pc_dashboard/utils/ring_buffer.py`

**Interfaces:**
- Produces: `RingBuffer(capacity)` class with `push(item)` / `get_all()` / `clear()` / `__len__()`

- [ ] **Step 1: Write `pc_dashboard/utils/ring_buffer.py`**

```python
""" 线程安全环形缓冲区，用于日志缓存 """

import threading
from typing import List, TypeVar

T = TypeVar('T')


class RingBuffer:
    """ 固定容量的线程安全环形缓冲区。满了以后自动覆盖最旧的数据。 """

    def __init__(self, capacity: int):
        if capacity < 1:
            raise ValueError("capacity must be >= 1")
        self._capacity = capacity
        self._buf: List[T] = [None] * capacity
        self._head = 0    # 下一个写入位置
        self._count = 0   # 当前元素数
        self._lock = threading.Lock()

    def push(self, item: T) -> None:
        """ 追加一个元素。若缓冲区已满，覆盖最旧的元素。 """
        with self._lock:
            self._buf[self._head] = item
            self._head = (self._head + 1) % self._capacity
            if self._count < self._capacity:
                self._count += 1

    def get_all(self) -> List[T]:
        """ 返回所有元素（从旧到新），不修改缓冲区。 """
        with self._lock:
            if self._count == 0:
                return []
            start = (self._head - self._count) % self._capacity
            result = []
            for i in range(self._count):
                idx = (start + i) % self._capacity
                result.append(self._buf[idx])
            return result

    def clear(self) -> None:
        """ 清空缓冲区。 """
        with self._lock:
            self._head = 0
            self._count = 0
            self._buf = [None] * self._capacity

    def __len__(self) -> int:
        with self._lock:
            return self._count

    @property
    def capacity(self) -> int:
        return self._capacity
```

- [ ] **Step 2: Verify in Python REPL**

```bash
python3 -c "
from pc_dashboard.utils.ring_buffer import RingBuffer
rb = RingBuffer(3)
rb.push('a'); rb.push('b'); rb.push('c')
assert rb.get_all() == ['a','b','c'], 'push/get_all failed'
rb.push('d')
assert rb.get_all() == ['b','c','d'], 'wrap failed'
rb.clear()
assert len(rb) == 0, 'clear failed'
print('RingBuffer: all checks PASS')
"
```

- [ ] **Step 3: Commit**

```bash
git add pc_dashboard/utils/ring_buffer.py
git commit -m "feat(pc_dashboard): add thread-safe ring buffer"
```

---

### Task 3: Frame Pack/Unpack (Python)

**Files:**
- Create: `pc_dashboard/protocol/cmd_frame.py`

**Interfaces:**
- Produces:
  - `crc8(data: bytes) -> int`
  - `sub_rsp(sub: int) -> int`
  - `sub_req(sub: int) -> int`
  - `pack(frame: dict) -> bytes` — raises ValueError on overflow
  - `parse(data: bytes) -> (dict|None, int)` — returns (frame, consumed) or (None, 0)

- [ ] **Step 1: Write `pc_dashboard/protocol/cmd_frame.py`**

```python
""" cmd 二进制帧打包/拆包 —— Python 实现，与 C 端 cmd_protocol.c 协议兼容 """

import struct
from typing import Optional, Tuple

from .cmd_defs import (
    CMD_FRAME_HEAD_MAGIC,
    CMD_FRAME_TAIL_MAGIC,
    CMD_FRAME_HEADER_SIZE,
    CMD_FRAME_CRC_SIZE,
    CMD_SUB_RESPONSE_FLAG,
    CMD_FRAME_MAX_PAYLOAD,
)


def crc8(data: bytes) -> int:
    """ XOR 校验和，与 C 端 cmd_crc8 完全一致 """
    crc = 0
    for b in data:
        crc ^= b
    return crc


def sub_rsp(sub: int) -> int:
    """ 从请求子命令生成响应子命令（bit7 置 1） """
    return sub | CMD_SUB_RESPONSE_FLAG


def sub_req(sub: int) -> int:
    """ 获取操作码（清除 bit7） """
    return sub & ~CMD_SUB_RESPONSE_FLAG


def pack(frame: dict) -> bytes:
    """
    将 frame dict 序列化为二进制帧。

    参数:
        frame: {"cmd": int, "sub": int, "len": int, "payload": bytes|None}

    返回:
        完整的二进制帧 bytes（含 HEAD + LEN + CMD + SUB + PAYLOAD + CRC）

    异常:
        ValueError: payload 长度超过 65535
    """
    payload = frame.get("payload") or b""
    payload_len = len(payload)

    if payload_len > 0xFFFF:
        raise ValueError(f"payload too large: {payload_len} > 65535")

    # HEAD(2B) + LEN(2B BE) + CMD(1B) + SUB(1B) + PAYLOAD(N B)
    header = struct.pack(
        ">BB HB B B",
        CMD_FRAME_HEAD_MAGIC,  # 0xA5
        CMD_FRAME_TAIL_MAGIC,  # 0x5A
        payload_len,            # LEN, big-endian uint16
        frame["cmd"],
        frame["sub"],
    )
    full = header + payload
    crc_byte = bytes([crc8(full)])
    return full + crc_byte


# 避免模块加载时报错（CMD_FRAME_MAX_PAYLOAD 在 cmd_defs 中不存在则回退）
try:
    CMD_FRAME_MAX_PAYLOAD
except NameError:
    CMD_FRAME_MAX_PAYLOAD = 65535


def parse(data: bytes) -> Tuple[Optional[dict], int]:
    """
    从字节流中解析一帧。支持帧同步（跳过前导垃圾字节）。

    参数:
        data: 原始字节流（可能含不完整帧或垃圾）

    返回:
        (frame_dict, consumed_bytes)
        - frame_dict: {"cmd", "sub", "len", "payload"} 或 None（数据不足）
        - consumed_bytes: 本次消费的字节数（仅在成功时 > 0）

    帧格式:
        HEAD(0xA5 0x5A) LEN(BE) CMD SUB PAYLOAD CRC(XOR)
    """
    total = len(data)
    if total < CMD_FRAME_HEADER_SIZE + CMD_FRAME_CRC_SIZE:
        return None, 0

    # 扫描帧头 0xA5 0x5A
    for offset in range(total - 1):
        if data[offset] == CMD_FRAME_HEAD_MAGIC and data[offset + 1] == CMD_FRAME_TAIL_MAGIC:
            break
    else:
        return None, 0  # 找不到帧头

    # 至少有 HEADER + 1B CRC
    min_len = offset + CMD_FRAME_HEADER_SIZE + CMD_FRAME_CRC_SIZE
    if total < min_len:
        return None, 0

    # 读取 LEN (big-endian uint16)
    payload_len = struct.unpack(">H", data[offset + 2:offset + 4])[0]

    frame_total = CMD_FRAME_HEADER_SIZE + payload_len + CMD_FRAME_CRC_SIZE
    if total - offset < frame_total:
        return None, 0  # 数据不完整

    frame_bytes = data[offset:offset + frame_total]

    # CRC 校验
    expected_crc = crc8(frame_bytes[:frame_total - 1])
    actual_crc = frame_bytes[frame_total - 1]
    if expected_crc != actual_crc:
        # CRC 不匹配，跳过 1 字节重新同步
        return None, offset + 1

    # 解析帧字段
    cmd = frame_bytes[4]
    sub = frame_bytes[5]
    payload = frame_bytes[6:6 + payload_len]

    return {
        "cmd": cmd,
        "sub": sub,
        "len": payload_len,
        "payload": payload,
    }, offset + frame_total
```

- [ ] **Step 2: Verify roundtrip**

```bash
python3 -c "
from pc_dashboard.protocol.cmd_frame import pack, parse, crc8
from pc_dashboard.protocol.cmd_defs import CMD_LED, CMD_SUB_WRITE

# 往返测试
f = {'cmd': CMD_LED, 'sub': CMD_SUB_WRITE, 'len': 2, 'payload': b'\x01\x01'}
b = pack(f)
assert b[0] == 0xA5 and b[1] == 0x5A, 'magic mismatch'
r, c = parse(b)
assert r is not None, 'parse returned None'
assert r['cmd'] == CMD_LED, 'cmd mismatch'
assert r['sub'] == CMD_SUB_WRITE, 'sub mismatch'
assert r['payload'] == b'\x01\x01', 'payload mismatch'

# 空 payload
f2 = {'cmd': 0x03, 'sub': 0x02, 'len': 0, 'payload': b''}
b2 = pack(f2)
r2, c2 = parse(b2)
assert r2 is not None and r2['payload'] == b'', 'empty payload fail'

# CRC 错误应跳过
bad = bytearray(b)
bad[-1] ^= 0xFF  # 破坏 CRC
r3, c3 = parse(bytes(bad))
assert r3 is None, 'CRC should fail'

# 带垃圾前缀
garbage = b'\xFF\xEE' + b
r4, c4 = parse(garbage)
assert r4 is not None and c4 == len(garbage), 'garbage skip fail'

print('cmd_frame: all checks PASS')
"
```

- [ ] **Step 3: Commit**

```bash
git add pc_dashboard/protocol/cmd_frame.py
git commit -m "feat(pc_dashboard): add frame pack/unpack with sync and CRC"
```

---

### Task 4: TCP Client with Background Reader

**Files:**
- Create: `pc_dashboard/protocol/cmd_client.py`

**Interfaces:**
- Produces: `CmdClient` class
  - `connect(host: str, port: int) -> bool`
  - `disconnect() -> None`
  - `send_request(cmd: int, sub: int, payload: bytes = b"") -> dict|None`
  - `subscribe(data_id: int, interval_ms: int) -> bool`
  - `unsubscribe(data_id: int) -> bool`
  - `set_push_callback(fn: callable) -> None`
  - `is_connected -> bool`
  - Signal: `connection_lost = Signal()`

- [ ] **Step 1: Write `pc_dashboard/protocol/cmd_client.py`**

```python
""" TCP 命令客户端 —— 连接管理 + 后台线程收帧。

请求-响应采用串行模式（一次只发一个请求），避免与板端 handler
payload 格式冲突。订阅推送在后台线程通过 push_callback 分发。"""

import socket
import struct
import threading
from typing import Optional, Callable

from .cmd_frame import pack, parse, sub_req
from .cmd_defs import (
    CMD_SENSOR,
    CMD_SUB_SUBSCRIBE,
    CMD_SUB_UNSUBSCRIBE,
    CMD_SYSTEM,
    CMD_SUB_LOG_SUBSCRIBE,
    CMD_SUB_LOG_UNSUBSCRIBE,
    CMD_DATA_LOG,
    CMD_ERR_OK,
)

# Qt 是可选的——允许在不加载 Qt 的测试环境中 import
try:
    from PySide6.QtCore import QObject, Signal
    _HAS_QT = True
except ImportError:
    _HAS_QT = False

    class QObject:
        pass

    class Signal:
        def __init__(self, *args):
            pass

        def emit(self, *args):
            pass

        def connect(self, fn):
            pass


class CmdClient(QObject if _HAS_QT else object):
    """
    TCP 命令客户端。

    连接开发板 cmd_server（默认端口 9527），发送二进制帧并接收响应/推送。

    线程模型:
      - 主线程调用 connect/send_request/subscribe/disconnect
      - 后台线程阻塞 socket.recv，解析帧，分发响应或推送回调
      - 串行请求: send_request 内部加锁，一次只允许一个请求在飞行中
    """

    if _HAS_QT:
        connection_lost = Signal()

    def __init__(self):
        super().__init__()
        self._sock: Optional[socket.socket] = None
        self._connected = False
        self._sock_lock = threading.Lock()     # 保护 _sock 的读写
        self._req_lock = threading.Lock()      # 串行化请求
        self._reader_thread: Optional[threading.Thread] = None
        self._reader_running = False
        self._push_callback: Optional[Callable[[dict], None]] = None

        # 当前等待中的请求
        self._resp_event = threading.Event()
        self._response: Optional[dict] = None

    # ── 连接管理 ──────────────────────────────────────────────

    def connect(self, host: str, port: int = 9527) -> bool:
        """ 连接到开发板 cmd_server。失败返回 False。 """
        self.disconnect()

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(3.0)
            sock.connect((host, port))
            sock.settimeout(None)  # 后台 recv 使用阻塞模式
        except (OSError, socket.timeout) as e:
            print(f"CmdClient: connect failed: {e}")
            return False

        with self._sock_lock:
            self._sock = sock
            self._connected = True
        self._resp_event.clear()
        self._response = None

        self._start_reader()
        return True

    def disconnect(self) -> None:
        """ 断开连接，停止后台线程。 """
        self._stop_reader()

        with self._sock_lock:
            if self._sock:
                try:
                    self._sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                try:
                    self._sock.close()
                except OSError:
                    pass
                self._sock = None
            self._connected = False
        self._resp_event.set()  # 唤醒正在等待的 send_request

    @property
    def is_connected(self) -> bool:
        return self._connected

    # ── 请求-响应（串行） ──────────────────────────────────────

    def send_request(self, cmd: int, sub: int, payload: bytes = b"") -> Optional[dict]:
        """
        发送请求帧并等待响应（3 秒超时）。

        串行保证: 同一时间只有一个请求在飞行中，响应顺序即为发送顺序。
        不会修改 payload，板端 handler 直接处理原始 payload。

        返回: 响应 frame dict，超时或失败返回 None
        """
        with self._req_lock:
            with self._sock_lock:
                if not self._sock or not self._connected:
                    return None
                sock = self._sock

            frame = {"cmd": cmd, "sub": sub, "len": len(payload), "payload": payload}

            try:
                data = pack(frame)
            except ValueError as e:
                print(f"CmdClient: pack failed: {e}")
                return None

            self._resp_event.clear()
            self._response = None

            try:
                sock.sendall(data)
            except OSError as e:
                print(f"CmdClient: send failed: {e}")
                self._on_connection_lost()
                return None

            # 等待响应（3s 超时）
            if not self._resp_event.wait(timeout=3.0):
                print("CmdClient: request timeout")
                return None

            return self._response

    # ── 订阅 ───────────────────────────────────────────────────

    def subscribe(self, data_id: int, interval_ms: int = 1000) -> bool:
        """
        订阅数据流（温度/湿度/日志）。

        data_id 为 CMD_DATA_TEMPERATURE/HUMIDITY 时内部使用 CMD_SENSOR。
        data_id 为 CMD_DATA_LOG 时内部使用 CMD_SYSTEM + CMD_SUB_LOG_SUBSCRIBE。
        """
        if data_id == CMD_DATA_LOG:
            # 日志订阅走 CMD_SYSTEM + LOG_SUBSCRIBE
            rsp = self.send_request(CMD_SYSTEM, CMD_SUB_LOG_SUBSCRIBE,
                                    struct.pack(">H", interval_ms))
        else:
            pl = struct.pack(">HH", data_id, interval_ms)
            rsp = self.send_request(CMD_SENSOR, CMD_SUB_SUBSCRIBE, pl)
        return rsp is not None and rsp.get("payload") and rsp["payload"][0] == CMD_ERR_OK

    def unsubscribe(self, data_id: int) -> bool:
        """ 取消数据流订阅。 """
        if data_id == CMD_DATA_LOG:
            rsp = self.send_request(CMD_SYSTEM, CMD_SUB_LOG_UNSUBSCRIBE,
                                    struct.pack(">H", data_id))
        else:
            rsp = self.send_request(CMD_SENSOR, CMD_SUB_UNSUBSCRIBE,
                                    struct.pack(">H", data_id))
        return rsp is not None and rsp.get("payload") and rsp["payload"][0] == CMD_ERR_OK

    # ── 推送回调 ───────────────────────────────────────────────

    def set_push_callback(self, fn: Optional[Callable[[dict], None]]) -> None:
        """ 设置推送帧回调（订阅数据 + 日志推送）。回调在后台线程中调用。 """
        self._push_callback = fn

    # ── 内部: 后台读取线程 ─────────────────────────────────────

    def _start_reader(self) -> None:
        """ 启动后台读取线程。 """
        if self._reader_thread and self._reader_thread.is_alive():
            return
        self._reader_running = True
        self._reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self._reader_thread.start()

    def _stop_reader(self) -> None:
        """ 停止后台读取线程。 """
        self._reader_running = False
        if self._reader_thread and self._reader_thread.is_alive():
            self._reader_thread.join(timeout=2.0)
        self._reader_thread = None

    def _reader_loop(self) -> None:
        """ 后台线程：阻塞读取 socket，解析帧并分发。 """
        buf = bytearray()

        while self._reader_running:
            try:
                with self._sock_lock:
                    sock = self._sock
                if not sock:
                    break

                chunk = sock.recv(4096)
                if not chunk:
                    # 对端关闭
                    print("CmdClient: connection closed by peer")
                    self._on_connection_lost()
                    break

                buf.extend(chunk)

                # 循环解析缓冲区中的所有帧
                while True:
                    frame, consumed = parse(bytes(buf))
                    if frame is None:
                        if consumed > 0:
                            # CRC 失败，跳过出错字节
                            del buf[:consumed]
                            continue
                        else:
                            break  # 数据不足，等更多数据

                    del buf[:consumed]
                    self._dispatch(frame)

            except (OSError, ConnectionResetError, ConnectionAbortedError) as e:
                print(f"CmdClient: read error: {e}")
                self._on_connection_lost()
                break

    def _on_connection_lost(self) -> None:
        """ 连接中断时的清理。从后台线程调用。 """
        with self._sock_lock:
            self._connected = False
            if self._sock:
                try:
                    self._sock.close()
                except OSError:
                    pass
                self._sock = None
        # 唤醒正在等待的 send_request
        self._resp_event.set()
        if _HAS_QT:
            self.connection_lost.emit()

    def _dispatch(self, frame: dict) -> None:
        """
        分发解析后的帧。

        规则:
          1. 如果正在等待响应（_resp_event 未设置），当作响应帧返回
          2. 否则视为推送帧，调用 push_callback
        """
        sub = frame["sub"]
        is_response = (sub & 0x80) != 0

        if is_response:
            if not self._resp_event.is_set():
                # 有请求在等待——这是它的响应
                self._response = frame
                self._resp_event.set()
            elif self._push_callback:
                # 推送帧（订阅数据、日志流等）
                self._push_callback(frame)
        # bit7=0 的请求帧：客户端不应收到，忽略
```

- [ ] **Step 2: Verify import**

```bash
python3 -c "from pc_dashboard.protocol.cmd_client import CmdClient; print('CmdClient OK')"
```

- [ ] **Step 3: Commit**

```bash
git add pc_dashboard/protocol/cmd_client.py
git commit -m "feat(pc_dashboard): add TCP client with background reader thread"
```

---

## Phase 2: Data Models

### Task 5: Data Models (Sensor, Led, System, Log)

**Files:**
- Create: `pc_dashboard/models/sensor_model.py`
- Create: `pc_dashboard/models/led_model.py`
- Create: `pc_dashboard/models/system_model.py`
- Create: `pc_dashboard/models/log_model.py`

**Interfaces:**
- Consumes: `CmdClient` from Task 4
- Produces: `SensorModel(CmdClient)`, `LedModel(CmdClient)`, `SystemModel(CmdClient)`, `LogModel(CmdClient)`
  - Each emits Qt signals to notify GUI of changes
  - Each holds an internal reference to `CmdClient` for sending requests

- [ ] **Step 1: Write `pc_dashboard/models/sensor_model.py`**

```python
""" 传感器数据模型 —— 温湿度缓存 + 订阅管理 """

import struct

from PySide6.QtCore import QObject, Signal

from pc_dashboard.protocol.cmd_defs import (
    CMD_SENSOR,
    CMD_SUB_READ,
    CMD_SUB_SUBSCRIBE,
    CMD_SUB_UNSUBSCRIBE,
    CMD_DATA_TEMPERATURE,
    CMD_DATA_HUMIDITY,
    CMD_ERR_OK,
)


class SensorModel(QObject):
    """ 温湿度数据模型。连接成功后自动读取初始值并订阅推送。 """

    temperature_updated = Signal(float)
    humidity_updated = Signal(float)
    subscription_changed = Signal(int, bool)  # data_id, is_subscribed

    def __init__(self, client):
        super().__init__()
        self._client = client
        self.temperature = 0.0
        self.humidity = 0.0
        self._subscribed: set = set()  # {CMD_DATA_TEMPERATURE, CMD_DATA_HUMIDITY}

    def on_connected(self) -> None:
        """ 连接成功后调用：读取初始值 + 自动订阅。 """
        # 读取初始值
        rsp = self._client.send_request(CMD_SENSOR, CMD_SUB_READ)
        if rsp and rsp.get("payload"):
            pl = rsp["payload"]
            if pl[0] == CMD_ERR_OK and rsp["len"] >= 9:
                temp_raw = struct.unpack(">I", pl[1:5])[0]
                hum_raw = struct.unpack(">I", pl[5:9])[0]
                # 网络字节序 → 主机 → float
                self.temperature = struct.unpack("!f", struct.pack(">I", temp_raw))[0]
                self.humidity = struct.unpack("!f", struct.pack(">I", hum_raw))[0]
                self.temperature_updated.emit(self.temperature)
                self.humidity_updated.emit(self.humidity)

        # 订阅温度推送
        if self._client.subscribe(CMD_DATA_TEMPERATURE, 1000):
            self._subscribed.add(CMD_DATA_TEMPERATURE)
            self.subscription_changed.emit(CMD_DATA_TEMPERATURE, True)

        # 订阅湿度推送
        if self._client.subscribe(CMD_DATA_HUMIDITY, 1000):
            self._subscribed.add(CMD_DATA_HUMIDITY)
            self.subscription_changed.emit(CMD_DATA_HUMIDITY, True)

    def on_disconnected(self) -> None:
        """ 连接断开后调用：清除订阅标记。 """
        self._subscribed.clear()

    def handle_push(self, frame: dict) -> bool:
        """
        处理推送帧。返回 True 表示已处理。

        推送帧 PAYLOAD = [data_id 2B BE, value N B]
        """
        sub = frame["sub"] & 0x7F
        if frame["cmd"] != CMD_SENSOR or sub != CMD_SUB_SUBSCRIBE:
            return False

        pl = frame.get("payload") or b""
        if len(pl) < 4:
            return False

        data_id = struct.unpack(">H", pl[0:2])[0]
        value_bytes = pl[2:]

        if data_id == CMD_DATA_TEMPERATURE and len(value_bytes) == 4:
            raw = struct.unpack(">I", value_bytes)[0]
            self.temperature = struct.unpack("!f", struct.pack(">I", raw))[0]
            self.temperature_updated.emit(self.temperature)
            return True

        elif data_id == CMD_DATA_HUMIDITY and len(value_bytes) == 4:
            raw = struct.unpack(">I", value_bytes)[0]
            self.humidity = struct.unpack("!f", struct.pack(">I", raw))[0]
            self.humidity_updated.emit(self.humidity)
            return True

        return False
```

- [ ] **Step 2: Write `pc_dashboard/models/led_model.py`**

```python
""" LED 数据模型 """

from PySide6.QtCore import QObject, Signal
import time

from pc_dashboard.protocol.cmd_defs import CMD_LED, CMD_SUB_WRITE, CMD_SUB_READ, CMD_ERR_OK


class LedModel(QObject):
    """ LED 状态模型，支持开关控制和状态查询。 """

    state_changed = Signal(int, bool)  # led_id, is_on

    def __init__(self, client):
        super().__init__()
        self._client = client
        self.led_state: dict = {}      # led_id -> bool
        self._last_action = 0.0        # 防抖时间戳
        self._debounce_ms = 0.5        # 500ms 防抖

    def on_connected(self) -> None:
        """ 连接成功后刷新 LED 状态。 """
        self.refresh()

    def on_disconnected(self) -> None:
        self.led_state.clear()

    def set_led(self, led_id: int, on: bool) -> bool:
        """ 开/关 LED。自动防抖。 """
        now = time.monotonic()
        if now - self._last_action < self._debounce_ms:
            return False  # 防抖期内忽略
        self._last_action = now

        state_byte = 0x01 if on else 0x00
        pl = bytes([led_id, state_byte])
        rsp = self._client.send_request(CMD_LED, CMD_SUB_WRITE, pl)
        if rsp and rsp.get("payload") and rsp["payload"][0] == CMD_ERR_OK:
            self.led_state[led_id] = on
            self.state_changed.emit(led_id, on)
            return True
        return False

    def refresh(self) -> bool:
        """ 查询所有 LED 状态（当前只支持 led_id=1）。 """
        rsp = self._client.send_request(CMD_LED, CMD_SUB_READ, bytes([1]))
        if rsp and rsp.get("payload") and rsp["payload"][0] == CMD_ERR_OK:
            pl = rsp["payload"]
            if len(pl) >= 3:
                led_id = pl[1]
                is_on = pl[2] != 0
                self.led_state[led_id] = is_on
                self.state_changed.emit(led_id, is_on)
                return True
        return False
```

- [ ] **Step 3: Write `pc_dashboard/models/system_model.py`**

```python
""" 系统信息数据模型 """

from PySide6.QtCore import QObject, Signal

from pc_dashboard.protocol.cmd_defs import (
    CMD_SYSTEM,
    CMD_SUB_READ,
    CMD_SUB_SET_LOGLEVEL,
    CMD_ERR_OK,
    LOG_LEVEL_DEBUG,
)


class SystemModel(QObject):
    """ 系统信息 + 日志等级控制。 """

    version_updated = Signal(str)
    loglevel_updated = Signal(int)

    def __init__(self, client):
        super().__init__()
        self._client = client
        self.version = ""
        self.log_level = LOG_LEVEL_DEBUG

    def on_connected(self) -> None:
        """ 连接成功后查询版本。 """
        rsp = self._client.send_request(CMD_SYSTEM, CMD_SUB_READ)
        if rsp and rsp.get("payload") and rsp["payload"][0] == CMD_ERR_OK:
            self.version = bytes(rsp["payload"][1:]).decode("utf-8", errors="replace")
            self.version_updated.emit(self.version)

    def set_log_level(self, level: int) -> bool:
        """ 设置日志等级 (0=DEBUG, 1=INFO, 2=WARN, 3=ERROR)。 """
        rsp = self._client.send_request(CMD_SYSTEM, CMD_SUB_SET_LOGLEVEL, bytes([level]))
        if rsp and rsp.get("payload") and rsp["payload"][0] == CMD_ERR_OK:
            self.log_level = level
            self.loglevel_updated.emit(level)
            return True
        return False
```

- [ ] **Step 4: Write `pc_dashboard/models/log_model.py`**

```python
""" 日志数据模型 —— 环形缓冲 + 等级过滤 """

import struct
import time
from datetime import datetime

from PySide6.QtCore import QObject, Signal

from pc_dashboard.protocol.cmd_defs import (
    CMD_SYSTEM,
    CMD_SUB_LOG_SUBSCRIBE,
    CMD_SUB_LOG_UNSUBSCRIBE,
    CMD_DATA_LOG,
    CMD_ERR_OK,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
)
from pc_dashboard.utils.ring_buffer import RingBuffer

_LOG_LEVEL_NAMES = {
    LOG_LEVEL_DEBUG: "DEBUG",
    LOG_LEVEL_INFO:  "INFO",
    LOG_LEVEL_WARN:  "WARN",
    LOG_LEVEL_ERROR: "ERROR",
}


class LogModel(QObject):
    """ 日志流模型。自动订阅日志推送，维护环形缓冲。 """

    log_received = Signal(int, str, str)  # level, timestamp_str, message
    log_cleared = Signal()

    def __init__(self, client):
        super().__init__()
        self._client = client
        self.buffer = RingBuffer(5000)  # (level, timestamp_str, msg) tuples
        self.filter_level = LOG_LEVEL_DEBUG
        self._subscribed = False
        self.error_count = 0  # 解析错误计数

    def on_connected(self) -> None:
        """ 连接成功后订阅日志推送。 """
        if self._client.subscribe(CMD_DATA_LOG, 0):
            self._subscribed = True
        else:
            print("LogModel: failed to subscribe to log stream")

    def on_disconnected(self) -> None:
        self._subscribed = False

    def handle_push(self, frame: dict) -> bool:
        """
        处理日志推送帧。返回 True 表示已处理。

        推送帧 PAYLOAD = [data_id 2B BE, level 1B, reserved 1B, timestamp 4B LE, msg N B]
        """
        sub = frame["sub"] & 0x7F
        if frame["cmd"] != CMD_SYSTEM or sub != CMD_SUB_LOG_SUBSCRIBE:
            return False

        pl = frame.get("payload") or b""
        if len(pl) < 4:
            return False

        data_id = struct.unpack(">H", pl[0:2])[0]
        if data_id != CMD_DATA_LOG:
            return False

        value = pl[2:]
        if len(value) < 6:  # level(1) + reserved(1) + timestamp(4)
            self.error_count += 1
            return False

        level = value[0]
        timestamp = struct.unpack("<I", value[2:6])[0]  # LE uint32
        msg = value[6:].decode("utf-8", errors="replace")

        ts_str = datetime.fromtimestamp(timestamp).strftime("%H:%M:%S")

        self.buffer.push((level, ts_str, msg))
        self.log_received.emit(level, ts_str, msg)
        return True

    def set_filter(self, level: int) -> None:
        """ 设置最低显示等级。只推送 >= level 的日志给 GUI。 """
        self.filter_level = level

    def clear(self) -> None:
        """ 清空缓冲区。 """
        self.buffer.clear()
        self.log_cleared.emit()

    @property
    def subscribed(self) -> bool:
        return self._subscribed

    @staticmethod
    def level_name(level: int) -> str:
        return _LOG_LEVEL_NAMES.get(level, "?????")
```

- [ ] **Step 5: Verify imports**

```bash
python3 -c "
from pc_dashboard.models.sensor_model import SensorModel
from pc_dashboard.models.led_model import LedModel
from pc_dashboard.models.system_model import SystemModel
from pc_dashboard.models.log_model import LogModel
print('All models import OK')
"
```

- [ ] **Step 6: Commit**

```bash
git add pc_dashboard/models/
git commit -m "feat(pc_dashboard): add sensor/led/system/log data models"
```

---

## Phase 3: GUI

### Task 6: Main Window with Connection Bar

**Files:**
- Create: `pc_dashboard/gui/main_window.py`

**Interfaces:**
- Consumes: `SystemModel`, `SensorModel`, `LedModel`, `LogModel`
- Produces: `MainWindow` class — QMainWindow with tab container, connection bar, status bar

- [ ] **Step 1: Write `pc_dashboard/gui/main_window.py`**

```python
""" 主窗口 —— 连接栏 + Tab 容器 + 状态栏 """

from PySide6.QtWidgets import (
    QMainWindow, QWidget, QHBoxLayout, QVBoxLayout, QLineEdit,
    QPushButton, QLabel, QTabWidget, QStatusBar, QApplication,
)
from PySide6.QtCore import Qt, QTimer

from pc_dashboard.protocol.cmd_client import CmdClient
from pc_dashboard.models.sensor_model import SensorModel
from pc_dashboard.models.led_model import LedModel
from pc_dashboard.models.system_model import SystemModel
from pc_dashboard.models.log_model import LogModel

CONNECTED_STYLE = "color: #27ae60; font-weight: bold;"
DISCONNECTED_STYLE = "color: #e74c3c; font-weight: bold;"


class MainWindow(QMainWindow):
    """ PC Dashboard 主窗口。 """

    def __init__(self):
        super().__init__()
        self.setWindowTitle("PC Dashboard")
        self.resize(900, 650)

        # 协议层
        self.client = CmdClient()

        # 数据模型
        self.sensor_model = SensorModel(self.client)
        self.led_model = LedModel(self.client)
        self.system_model = SystemModel(self.client)
        self.log_model = LogModel(self.client)

        # 推送路由
        self.client.set_push_callback(self._on_push)

        # 连接断开回调
        self.client.connection_lost.connect(self._on_connection_lost)

        self._setup_ui()
        self._connect_signals()

    def _setup_ui(self) -> None:
        """ 构建 UI 布局。 """
        central = QWidget()
        self.setCentralWidget(central)
        main_layout = QVBoxLayout(central)
        main_layout.setContentsMargins(10, 10, 10, 10)

        # ── 顶部连接栏 ──
        conn_layout = QHBoxLayout()

        conn_layout.addWidget(QLabel("Host:"))
        self.host_input = QLineEdit("192.168.3.171")
        self.host_input.setFixedWidth(140)
        conn_layout.addWidget(self.host_input)

        conn_layout.addWidget(QLabel("Port:"))
        self.port_input = QLineEdit("9527")
        self.port_input.setFixedWidth(60)
        conn_layout.addWidget(self.port_input)

        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self._on_connect)
        conn_layout.addWidget(self.connect_btn)

        self.reconnect_btn = QPushButton("Reconnect")
        self.reconnect_btn.clicked.connect(self._on_connect)
        self.reconnect_btn.setEnabled(False)
        conn_layout.addWidget(self.reconnect_btn)

        conn_layout.addStretch()

        self.status_label = QLabel("⚫ Disconnected")
        self.status_label.setStyleSheet(DISCONNECTED_STYLE)
        conn_layout.addWidget(self.status_label)

        main_layout.addLayout(conn_layout)

        # ── Tab 容器 ──
        self.tabs = QTabWidget()

        # 各 tab 在后续任务中实现，此处先做占位
        from pc_dashboard.gui.dashboard_tab import DashboardTab
        from pc_dashboard.gui.log_tab import LogTab
        from pc_dashboard.gui.system_tab import SystemTab

        self.dashboard_tab = DashboardTab(self.sensor_model, self.led_model)
        self.log_tab = LogTab(self.log_model)
        self.system_tab = SystemTab(self.system_model)

        self.tabs.addTab(self.dashboard_tab, "仪表盘")
        self.tabs.addTab(self.log_tab, "日志")
        self.tabs.addTab(self.system_tab, "系统")

        main_layout.addWidget(self.tabs, stretch=1)

        # ── 底部状态栏 ──
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)
        self.status_bar.showMessage("未连接")

    def _connect_signals(self) -> None:
        """ 连接数据模型信号到状态栏更新。 """
        self.sensor_model.temperature_updated.connect(
            lambda v: self._update_status_summary()
        )
        self.sensor_model.humidity_updated.connect(
            lambda v: self._update_status_summary()
        )

    def _on_connect(self) -> None:
        """ 连接/重连按钮回调。 """
        host = self.host_input.text().strip()
        try:
            port = int(self.port_input.text().strip())
        except ValueError:
            self.status_bar.showMessage("端口号无效")
            return

        self.status_bar.showMessage(f"正在连接 {host}:{port} ...")
        QApplication.processEvents()

        if self.client.connect(host, port):
            self.status_label.setText("● Connected")
            self.status_label.setStyleSheet(CONNECTED_STYLE)
            self.connect_btn.setEnabled(False)
            self.reconnect_btn.setEnabled(True)
            self.status_bar.showMessage(f"已连接 {host}:{port}")

            # 通知各模型连接成功
            self.sensor_model.on_connected()
            self.led_model.on_connected()
            self.system_model.on_connected()
            self.log_model.on_connected()
        else:
            self.status_label.setText("⚫ Disconnected")
            self.status_bar.showMessage("连接失败")

    def _on_connection_lost(self) -> None:
        """ 连接断开的回调（从后台线程触发）。 """
        self.status_label.setText("⚫ Disconnected")
        self.status_label.setStyleSheet(DISCONNECTED_STYLE)
        self.connect_btn.setEnabled(True)
        self.reconnect_btn.setEnabled(False)
        self.status_bar.showMessage("连接断开")

        self.sensor_model.on_disconnected()
        self.led_model.on_disconnected()
        self.log_model.on_disconnected()

    def _on_push(self, frame: dict) -> None:
        """ 推送帧分发到各模型。 """
        # 按 cmd 分发
        self.sensor_model.handle_push(frame)
        self.log_model.handle_push(frame)

    def _update_status_summary(self) -> None:
        """ 更新底部状态栏摘要。 """
        if self.client.is_connected:
            self.status_bar.showMessage(
                f"已连接 | 温度: {self.sensor_model.temperature:.1f}°C "
                f"| 湿度: {self.sensor_model.humidity:.1f}%"
            )

    def closeEvent(self, event) -> None:
        """ 窗口关闭时断开连接。 """
        self.client.disconnect()
        super().closeEvent(event)
```

- [ ] **Step 2: Verify import** (tab imports will fail until next tasks)

```bash
python3 -c "from pc_dashboard.gui.main_window import MainWindow; print('MainWindow OK')"
```

Note: 这会因依赖 dashboard_tab/log_tab/system_tab 而失败——这是预期的。Task 6-9 完成后整体验证。

- [ ] **Step 3: Commit**

```bash
git add pc_dashboard/gui/main_window.py
git commit -m "feat(pc_dashboard): add main window with connection bar"
```

---

### Task 7: Dashboard Tab (Sensor Cards + LED Control)

**Files:**
- Create: `pc_dashboard/gui/dashboard_tab.py`

- [ ] **Step 1: Write `pc_dashboard/gui/dashboard_tab.py`**

```python
""" 仪表盘标签页 —— 温湿度卡片 + LED 控制 """

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox,
    QPushButton, QLabel, QFrame,
)
from PySide6.QtCore import Qt

TEMP_NORMAL = "color: #2c3e50;"
TEMP_HOT = "color: #e74c3c; font-weight: bold;"
HUM_NORMAL = "color: #2c3e50;"
HUM_LOW = "color: #f39c12; font-weight: bold;"
CARD_STYLE = """
QFrame#card {
    background: #f8f9fa;
    border: 1px solid #dee2e6;
    border-radius: 10px;
    padding: 15px;
}
"""
SUB_ON = "● 订阅中  "
SUB_OFF = "○ 未订阅  "


class DashboardTab(QWidget):
    """ 仪表盘: 温湿度大字体卡片 + LED 开关控制。 """

    def __init__(self, sensor_model, led_model):
        super().__init__()
        self._sensor = sensor_model
        self._led = led_model

        self._setup_ui()
        self._connect_signals()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)

        # ── 温湿度卡片 ──
        cards_layout = QHBoxLayout()

        # 温度卡片
        temp_card = QFrame()
        temp_card.setObjectName("card")
        temp_card.setStyleSheet(CARD_STYLE)
        temp_layout = QVBoxLayout(temp_card)
        temp_title = QLabel("温度 °C")
        temp_title.setAlignment(Qt.AlignCenter)
        temp_title.setStyleSheet("font-size: 14px; color: #7f8c8d;")
        self.temp_value = QLabel("--.-")
        self.temp_value.setAlignment(Qt.AlignCenter)
        self.temp_value.setStyleSheet("font-size: 48px; " + TEMP_NORMAL)
        self.temp_status = QLabel(SUB_OFF)
        self.temp_status.setAlignment(Qt.AlignCenter)
        self.temp_status.setStyleSheet("color: #95a5a6;")
        temp_layout.addWidget(temp_title)
        temp_layout.addWidget(self.temp_value)
        temp_layout.addWidget(self.temp_status)
        cards_layout.addWidget(temp_card)

        # 湿度卡片
        hum_card = QFrame()
        hum_card.setObjectName("card")
        hum_card.setStyleSheet(CARD_STYLE)
        hum_layout = QVBoxLayout(hum_card)
        hum_title = QLabel("湿度 %RH")
        hum_title.setAlignment(Qt.AlignCenter)
        hum_title.setStyleSheet("font-size: 14px; color: #7f8c8d;")
        self.hum_value = QLabel("--.-")
        self.hum_value.setAlignment(Qt.AlignCenter)
        self.hum_value.setStyleSheet("font-size: 48px; " + HUM_NORMAL)
        self.hum_status = QLabel(SUB_OFF)
        self.hum_status.setAlignment(Qt.AlignCenter)
        self.hum_status.setStyleSheet("color: #95a5a6;")
        hum_layout.addWidget(hum_title)
        hum_layout.addWidget(self.hum_value)
        hum_layout.addWidget(self.hum_status)
        cards_layout.addWidget(hum_card)

        layout.addLayout(cards_layout)

        # ── LED 控制 ──
        led_group = QGroupBox("LED 控制")
        led_layout = QVBoxLayout(led_group)

        led_info = QHBoxLayout()
        led_info.addWidget(QLabel("blue_led"))
        self.led_state_label = QLabel("● OFF")
        self.led_state_label.setStyleSheet("color: #95a5a6; font-weight: bold;")
        led_info.addWidget(self.led_state_label)
        led_info.addStretch()
        led_layout.addLayout(led_info)

        btn_layout = QHBoxLayout()
        self.led_on_btn = QPushButton("ON")
        self.led_on_btn.clicked.connect(lambda: self._set_led(True))
        self.led_off_btn = QPushButton("OFF")
        self.led_off_btn.clicked.connect(lambda: self._set_led(False))
        btn_layout.addWidget(self.led_on_btn)
        btn_layout.addWidget(self.led_off_btn)
        btn_layout.addStretch()
        led_layout.addLayout(btn_layout)

        layout.addWidget(led_group)
        layout.addStretch()

    def _connect_signals(self) -> None:
        """ 连接模型信号。 """
        self._sensor.temperature_updated.connect(self._on_temp)
        self._sensor.humidity_updated.connect(self._on_hum)
        self._sensor.subscription_changed.connect(self._on_sub)
        self._led.state_changed.connect(self._on_led)

    def _on_temp(self, value: float) -> None:
        self.temp_value.setText(f"{value:.1f}")
        if value > 40.0:
            self.temp_value.setStyleSheet("font-size: 48px; " + TEMP_HOT)
        else:
            self.temp_value.setStyleSheet("font-size: 48px; " + TEMP_NORMAL)

    def _on_hum(self, value: float) -> None:
        self.hum_value.setText(f"{value:.1f}")
        if value < 30.0:
            self.hum_value.setStyleSheet("font-size: 48px; " + HUM_LOW)
        else:
            self.hum_value.setStyleSheet("font-size: 48px; " + HUM_NORMAL)

    def _on_sub(self, data_id: int, is_subscribed: bool) -> None:
        from pc_dashboard.protocol.cmd_defs import CMD_DATA_TEMPERATURE

        status = SUB_ON if is_subscribed else SUB_OFF
        if data_id == CMD_DATA_TEMPERATURE:
            self.temp_status.setText(status)
            self.temp_status.setStyleSheet(
                "color: #27ae60;" if is_subscribed else "color: #95a5a6;"
            )
        else:
            self.hum_status.setText(status)
            self.hum_status.setStyleSheet(
                "color: #27ae60;" if is_subscribed else "color: #95a5a6;"
            )

    def _on_led(self, led_id: int, is_on: bool) -> None:
        if is_on:
            self.led_state_label.setText("● ON")
            self.led_state_label.setStyleSheet("color: #27ae60; font-weight: bold;")
        else:
            self.led_state_label.setText("● OFF")
            self.led_state_label.setStyleSheet("color: #95a5a6; font-weight: bold;")

    def _set_led(self, on: bool) -> None:
        """ 发送 LED 开关命令。 """
        self._led.set_led(1, on)  # led_id=1
```

- [ ] **Step 2: Commit**

```bash
git add pc_dashboard/gui/dashboard_tab.py
git commit -m "feat(pc_dashboard): add dashboard tab with sensor cards and LED control"
```

---

### Task 8: Log Tab

**Files:**
- Create: `pc_dashboard/gui/log_tab.py`

- [ ] **Step 1: Write `pc_dashboard/gui/log_tab.py`**

```python
""" 日志流标签页 —— 等级过滤 + 彩色标签 + 暂停/自动滚动 """

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QTableWidget,
    QTableWidgetItem, QComboBox, QPushButton, QLabel, QHeaderView,
)
from PySide6.QtCore import Qt
from PySide6.QtGui import QColor

from pc_dashboard.protocol.cmd_defs import (
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
)

_LEVEL_COLORS = {
    LOG_LEVEL_DEBUG: QColor("#95a5a6"),  # 灰色
    LOG_LEVEL_INFO:  QColor("#2980b9"),  # 蓝色
    LOG_LEVEL_WARN:  QColor("#f39c12"),  # 橙色
    LOG_LEVEL_ERROR: QColor("#e74c3c"),  # 红色
}

FILTERS = [
    ("ALL",   LOG_LEVEL_DEBUG),
    ("INFO+", LOG_LEVEL_INFO),
    ("WARN+", LOG_LEVEL_WARN),
    ("ERROR", LOG_LEVEL_ERROR),
]


class LogTab(QWidget):
    """ 日志流页面。 """

    def __init__(self, log_model):
        super().__init__()
        self._log = log_model
        self._paused = False
        self._auto_scroll = True

        self._setup_ui()
        self._connect_signals()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)

        # ── 工具栏 ──
        toolbar = QHBoxLayout()

        toolbar.addWidget(QLabel("等级过滤:"))
        self.filter_combo = QComboBox()
        for label, _ in FILTERS:
            self.filter_combo.addItem(label)
        self.filter_combo.currentIndexChanged.connect(self._on_filter_changed)
        toolbar.addWidget(self.filter_combo)

        self.clear_btn = QPushButton("清空")
        self.clear_btn.clicked.connect(self._log.clear)
        toolbar.addWidget(self.clear_btn)

        self.pause_btn = QPushButton("暂停")
        self.pause_btn.setCheckable(True)
        self.pause_btn.toggled.connect(self._on_pause)
        toolbar.addWidget(self.pause_btn)

        toolbar.addStretch()

        self.count_label = QLabel("共 0 条")
        toolbar.addWidget(self.count_label)

        layout.addLayout(toolbar)

        # ── 日志表格 ──
        self.table = QTableWidget()
        self.table.setColumnCount(3)
        self.table.setHorizontalHeaderLabels(["时间", "等级", "消息"])
        self.table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(2, QHeaderView.Stretch)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        self.table.setShowGrid(False)
        self.table.verticalHeader().setVisible(False)
        self.table.setAlternatingRowColors(True)

        # 垂直滚动条移动 → 检测用户手动上滚时暂停自动滚动
        self.table.verticalScrollBar().valueChanged.connect(self._on_scroll)

        layout.addWidget(self.table)

    def _connect_signals(self) -> None:
        self._log.log_received.connect(self._on_log)
        self._log.log_cleared.connect(self._on_cleared)

    def _on_log(self, level: int, ts: str, msg: str) -> None:
        """ 收到一条日志。 """
        # 等级过滤
        if level < self._log.filter_level:
            return

        row = self.table.rowCount()
        self.table.insertRow(row)

        # 时间
        time_item = QTableWidgetItem(ts)
        self.table.setItem(row, 0, time_item)

        # 等级
        lvl_name = self._log.level_name(level)
        lvl_item = QTableWidgetItem(lvl_name)
        lvl_item.setForeground(_LEVEL_COLORS.get(level, QColor("#000000")))
        self.table.setItem(row, 1, lvl_item)

        # 消息
        msg_item = QTableWidgetItem(msg)
        self.table.setItem(row, 2, msg_item)

        # 自动滚动到底部
        if not self._paused and self._auto_scroll:
            self.table.scrollToBottom()

        self._update_count()

    def _on_cleared(self) -> None:
        self.table.setRowCount(0)
        self._update_count()

    def _on_filter_changed(self, index: int) -> None:
        _, level = FILTERS[index]
        self._log.set_filter(level)
        # 重建表格显示
        self._rebuild_table()

    def _on_pause(self, checked: bool) -> None:
        self._paused = checked
        self.pause_btn.setText("继续" if checked else "暂停")
        if not checked:
            self._rebuild_table()
            self.table.scrollToBottom()

    def _on_scroll(self, value: int) -> None:
        """ 检测用户手动上滚——暂停自动滚动。 """
        scrollbar = self.table.verticalScrollBar()
        if scrollbar.maximum() - value > 20:
            self._auto_scroll = False
        else:
            self._auto_scroll = True

    def _rebuild_table(self) -> None:
        """ 根据当前过滤器重建表格。 """
        self.table.setRowCount(0)
        for level, ts, msg in self._log.buffer.get_all():
            if level >= self._log.filter_level:
                row = self.table.rowCount()
                self.table.insertRow(row)

                time_item = QTableWidgetItem(ts)
                self.table.setItem(row, 0, time_item)

                lvl_item = QTableWidgetItem(self._log.level_name(level))
                lvl_item.setForeground(_LEVEL_COLORS.get(level, QColor("#000000")))
                self.table.setItem(row, 1, lvl_item)

                self.table.setItem(row, 2, QTableWidgetItem(msg))

        self._update_count()

    def _update_count(self) -> None:
        total = len(self._log.buffer)
        shown = self.table.rowCount()
        self.count_label.setText(f"共 {total} 条，显示 {shown} 条")
```

- [ ] **Step 2: Commit**

```bash
git add pc_dashboard/gui/log_tab.py
git commit -m "feat(pc_dashboard): add log tab with level filter and color coding"
```

---

### Task 9: System Tab

**Files:**
- Create: `pc_dashboard/gui/system_tab.py`

- [ ] **Step 1: Write `pc_dashboard/gui/system_tab.py`**

```python
""" 系统信息标签页 —— 版本 + 日志等级控制 + 连接统计 """

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QGroupBox, QHBoxLayout,
    QLabel, QRadioButton, QPushButton, QButtonGroup,
)
from PySide6.QtCore import Qt

from pc_dashboard.protocol.cmd_defs import (
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
)

LEVELS = [
    ("DEBUG", LOG_LEVEL_DEBUG),
    ("INFO",  LOG_LEVEL_INFO),
    ("WARN",  LOG_LEVEL_WARN),
    ("ERROR", LOG_LEVEL_ERROR),
]


class SystemTab(QWidget):
    """ 系统页面：版本信息、日志等级控制、连接统计。 """

    def __init__(self, system_model):
        super().__init__()
        self._sys = system_model
        self._setup_ui()
        self._connect_signals()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)

        # ── 系统信息 ──
        info_group = QGroupBox("系统信息")
        info_layout = QVBoxLayout(info_group)

        self.version_label = QLabel("固件版本: ---")
        self.version_label.setStyleSheet("font-size: 14px;")
        info_layout.addWidget(self.version_label)

        self.conn_label = QLabel("连接方式: ---")
        info_layout.addWidget(self.conn_label)

        self.status_label = QLabel("连接状态: ⚫ 未连接")
        info_layout.addWidget(self.status_label)

        layout.addWidget(info_group)

        # ── 日志等级 ──
        log_group = QGroupBox("日志等级")
        log_layout = QVBoxLayout(log_group)

        radio_layout = QHBoxLayout()
        self.level_group = QButtonGroup(self)
        self.radios = {}
        for label, level in LEVELS:
            radio = QRadioButton(label)
            self.radios[level] = radio
            self.level_group.addButton(radio, level)
            radio_layout.addWidget(radio)
        log_layout.addLayout(radio_layout)

        apply_btn = QPushButton("应用")
        apply_btn.clicked.connect(self._on_apply)
        apply_btn.setFixedWidth(80)
        log_layout.addWidget(apply_btn)

        self.level_status = QLabel("")
        self.level_status.setStyleSheet("color: #27ae60;")
        log_layout.addWidget(self.level_status)

        layout.addWidget(log_group)

        # ── 统计 ──
        stat_group = QGroupBox("统计")
        stat_layout = QVBoxLayout(stat_group)
        self.stat_label = QLabel(
            "仪表盘刷新率: --\n日志接收: --\n连接时长: --"
        )
        stat_layout.addWidget(self.stat_label)
        layout.addWidget(stat_group)

        layout.addStretch()

    def _connect_signals(self) -> None:
        self._sys.version_updated.connect(self._on_version)
        self._sys.loglevel_updated.connect(self._on_loglevel)

    def _on_version(self, version: str) -> None:
        self.version_label.setText(f"固件版本: {version}")
        self.status_label.setText("连接状态: ● 已连接")
        self.status_label.setStyleSheet("color: #27ae60;")

    def _on_loglevel(self, level: int) -> None:
        if level in self.radios:
            self.radios[level].setChecked(True)
        self.level_status.setText(
            f"当前等级: {dict(LEVELS).get(level, '?')}  ✓"
        )

    def _on_apply(self) -> None:
        level = self.level_group.checkedId()
        if level >= 0:
            if self._sys.set_log_level(level):
                self.level_status.setText(
                    f"已设置为: {dict(LEVELS).get(level, '?')}  ✓"
                )
                self.level_status.setStyleSheet("color: #27ae60;")
            else:
                self.level_status.setText("设置失败  ✗")
                self.level_status.setStyleSheet("color: #e74c3c;")

    def update_connection_info(self, host: str, port: int, connected: bool) -> None:
        """ 由 MainWindow 调用，更新连接信息。 """
        if connected:
            self.conn_label.setText(f"连接方式: TCP ({host}:{port})")
            self.status_label.setText("连接状态: ● 已连接")
            self.status_label.setStyleSheet("color: #27ae60;")
        else:
            self.conn_label.setText("连接方式: ---")
            self.status_label.setText("连接状态: ⚫ 未连接")
            self.status_label.setStyleSheet("color: #95a5a6;")
```

- [ ] **Step 2: Commit**

```bash
git add pc_dashboard/gui/system_tab.py
git commit -m "feat(pc_dashboard): add system tab with version info and log level control"
```

---

### Task 10: Entry Point

**Files:**
- Create: `pc_dashboard/main.py`

- [ ] **Step 1: Write `pc_dashboard/main.py`**

```python
#!/usr/bin/env python3
""" PC Dashboard —— Orange Pi 5 Plus 状态监控上位机 """

import sys
from PySide6.QtWidgets import QApplication
from pc_dashboard.gui.main_window import MainWindow


def main():
    app = QApplication(sys.argv)
    app.setApplicationName("PC Dashboard")
    app.setStyle("Fusion")  # 跨平台一致外观

    window = MainWindow()
    window.show()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Make executable**

```bash
chmod +x pc_dashboard/main.py
```

- [ ] **Step 3: Verify all imports**

```bash
python3 -c "
from pc_dashboard.gui.main_window import MainWindow
from pc_dashboard.gui.dashboard_tab import DashboardTab
from pc_dashboard.gui.log_tab import LogTab
from pc_dashboard.gui.system_tab import SystemTab
from pc_dashboard.protocol.cmd_client import CmdClient
from pc_dashboard.protocol.cmd_frame import pack, parse
from pc_dashboard.protocol.cmd_defs import CMD_LED, CMD_SENSOR, CMD_SYSTEM
from pc_dashboard.utils.ring_buffer import RingBuffer
print('All modules imported successfully')
print('PC Dashboard is ready for use')
"
```

- [ ] **Step 4: Commit**

```bash
git add pc_dashboard/main.py
git commit -m "feat(pc_dashboard): add entry point main.py"
```

---

## Phase 4: Board-Side Changes (C)

### Task 11: cmd_frame.h — Add Log Constants

**Files:**
- Modify: `modules/cmd/include/cmd/cmd_frame.h`

- [ ] **Step 1: Add new defines**

In `cmd_frame.h`, after `CMD_DATA_HUMIDITY` and before the error code section:

```c
#define CMD_DATA_LOG              0x0003  /* 日志推送         */
#define CMD_SUB_LOG_SUBSCRIBE     0x05    /* 订阅日志推送     */
#define CMD_SUB_LOG_UNSUBSCRIBE   0x06    /* 取消日志订阅     */
```

- [ ] **Step 2: Verify compilation**

```bash
./build.sh
```

- [ ] **Step 3: Commit**

```bash
git add modules/cmd/include/cmd/cmd_frame.h
git commit -m "feat(cmd): add CMD_DATA_LOG and LOG_SUBSCRIBE/UNSUBSCRIBE constants"
```

---

### Task 12: Log Module — Ring Buffer + Subscribe Callback

**Files:**
- Modify: `modules/log/include/log/log.h`
- Modify: `modules/log/src/log.c`

- [ ] **Step 1: Add ring buffer + callback API to `log.h`**

After the `log_level_t` enum, add:

```c
/* ── 日志订阅回调 ────────────────────────────────────────────────── */

/** 日志订阅回调类型。每条日志写入时调用。 */
typedef void (*log_subscribe_fn_t)(uint8_t level, const char* msg, void* ctx);

/**
 * 设置日志订阅回调。
 *
 * 设置后，每条通过 LOG_* 宏写入的日志在写文件之后会调用此回调。
 * 传 NULL 取消回调。
 *
 * @param cb   回调函数指针，NULL 取消
 * @param ctx  用户上下文，透传给回调
 * @note       回调在持有日志锁的情况下调用，回调内部不应再调用 LOG_* 宏
 */
void log_set_subscribe_callback(log_subscribe_fn_t cb, void* ctx);
```

And add the ring buffer constants + accessor:

```c
/* ── 环形缓冲区 ──────────────────────────────────────────────────── */

#define LOG_RING_SIZE  2048  /* 环形缓冲区容量（条数）               */
#define LOG_MSG_MAX    256   /* 单条日志消息最大长度                 */

/** 环形缓冲区条目。 */
typedef struct {
    uint8_t  level;
    uint8_t  reserved;
    uint32_t timestamp;     /* unix timestamp，LE */
    char     msg[LOG_MSG_MAX];
} log_ring_entry_t;

/**
 * 从环形缓冲区获取已缓存的日志。
 *
 * @param out_entries  输出数组（调用者分配，至少 LOG_RING_SIZE 大小）
 * @param out_count    输出实际条目数
 * @note               线程安全
 */
void log_ring_get_all(log_ring_entry_t* out_entries, int* out_count);
```

- [ ] **Step 2: Implement ring buffer + callback in `log.c`**

Add to global state (after `g_log`):

```c
/* 环形缓冲区 */
static log_ring_entry_t  g_log_ring[LOG_RING_SIZE];
static int               g_log_ring_head  = 0;  /* 下一个写入位置 */
static int               g_log_ring_count = 0;  /* 当前条目数     */

/* 订阅回调 */
static log_subscribe_fn_t g_log_sub_cb  = NULL;
static void*              g_log_sub_ctx = NULL;
```

Add implementation functions (before `log_init`):

```c
/* ── 订阅回调 API ────────────────────────────────────────────────── */

/**
 * 设置日志订阅回调。
 *
 * @param cb   回调函数指针
 * @param ctx  用户上下文
 * @note       线程安全（在锁内设置）
 */
void log_set_subscribe_callback(log_subscribe_fn_t cb, void* ctx)
{
    hw_err_t ret = hw_mutex_lock(&g_log.lock);
    if (ret != HW_OK) {
        fprintf(stderr, "log: lock failed in log_set_subscribe_callback\n");
        return;
    }
    g_log_sub_cb  = cb;
    g_log_sub_ctx = ctx;
    hw_mutex_unlock(&g_log.lock);
}

/* ── 环形缓冲区 API ──────────────────────────────────────────────── */

/**
 * 从环形缓冲区获取所有已缓存的日志条目。
 *
 * 条目按时间顺序（从旧到新）复制到 out_entries。
 *
 * @param out_entries  输出缓冲区
 * @param out_count    输出实际条目数
 * @note               在锁内复制，调用方不需加锁
 */
void log_ring_get_all(log_ring_entry_t* out_entries, int* out_count)
{
    if (!out_entries || !out_count) return;

    hw_err_t ret = hw_mutex_lock(&g_log.lock);
    if (ret != HW_OK) {
        *out_count = 0;
        return;
    }

    *out_count = g_log_ring_count;
    if (g_log_ring_count > 0) {
        int start = (g_log_ring_head - g_log_ring_count + LOG_RING_SIZE) % LOG_RING_SIZE;
        for (int i = 0; i < g_log_ring_count; i++) {
            out_entries[i] = g_log_ring[(start + i) % LOG_RING_SIZE];
        }
    }

    hw_mutex_unlock(&g_log.lock);
}

/**
 * 内部：向环形缓冲区写入一条日志。
 * 调用者必须持有 g_log.lock。
 */
static void _log_ring_push(uint8_t level, const char* msg)
{
    log_ring_entry_t* entry = &g_log_ring[g_log_ring_head];
    entry->level     = level;
    entry->reserved  = 0;
    entry->timestamp = (uint32_t)time(NULL);
    strncpy(entry->msg, msg, LOG_MSG_MAX - 1);
    entry->msg[LOG_MSG_MAX - 1] = '\0';

    g_log_ring_head = (g_log_ring_head + 1) % LOG_RING_SIZE;
    if (g_log_ring_count < LOG_RING_SIZE) {
        g_log_ring_count++;
    }
}
```

- [ ] **Step 3: Modify `log_write_impl`** to call ring buffer + callback

In `log_write_impl`, after the `fprintf(stdout, ...)` block and before `hw_mutex_unlock`, add:

```c
    /* 写入环形缓冲区并通知订阅回调 */
    _log_ring_push((uint8_t)level, msg);

    if (g_log_sub_cb) {
        g_log_sub_cb((uint8_t)level, msg, g_log_sub_ctx);
    }
```

Note: remove the `LOG_INFO` from `log_init` that would trigger the callback before initialization is complete — or keep it and ensure `_log_ring_push` works before lock is initialized. Better approach: move `_log_ring_push` call inside the lock block so it's protected, and ensure `msg` is constructed before entering the lock (it already is — the `msg[512]` buffer exists at that point).

Actually, looking at the current `log_write_impl`, the `msg` variable IS inside the lock block already. And `_log_ring_push` uses `strncpy` which needs `msg` to still be valid. It is — `msg[512]` is on the stack and the callback is called while still holding the lock. The callback (`on_log_push` in main.c) should NOT call any LOG_* macros to avoid deadlock — this is documented in the `log_set_subscribe_callback` note.

- [ ] **Step 4: Verify compilation**

```bash
./build.sh
```

- [ ] **Step 5: Commit**

```bash
git add modules/log/include/log/log.h modules/log/src/log.c
git commit -m "feat(log): add ring buffer and subscribe callback for log push"
```

---

### Task 13: cmd_handler_system — Log Subscribe/Unsubscribe

**Files:**
- Modify: `modules/cmd/src/cmd_handler_system.c`

- [ ] **Step 1: Add log subscription handling**

In `cmd_handler_system`, add two new `else if` branches before the final `else`:

```c
#include "cmd/cmd_subscription.h"  /* 新增 include */
#include "log/log.h"               /* 已有 */
#include <time.h>                  /* time_t */

    } else if (op == CMD_SUB_LOG_SUBSCRIBE) {
        /* 订阅日志推送: 先推送缓冲日志(最多500条)，再注册实时推送 */
        cmd_subscription_mgr_t* sub_mgr = (cmd_subscription_mgr_t*)ctx;

        if (!sub_mgr) {
            uint8_t err = CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_SYSTEM, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        /* 注册订阅 */
        cmd_subscription_add(sub_mgr, CMD_DATA_LOG, 0, conn);

        /* 推送已缓存的日志（最多 500 条） */
        log_ring_entry_t entries[500];
        int count = 0;
        log_ring_get_all(entries, &count);
        if (count > 500) count = 500;

        for (int i = 0; i < count; i++) {
            /* 构造推送帧: [data_id 2B BE, level 1B, reserved 1B, timestamp 4B LE, msg N B] */
            size_t msg_len = strlen(entries[i].msg);
            size_t pld_len = 2 + 1 + 1 + 4 + msg_len;
            uint8_t* pld = (uint8_t*)malloc(pld_len);
            if (!pld) continue;

            uint16_t id_be = htons(CMD_DATA_LOG);
            uint8_t* p = pld;
            memcpy(p, &id_be, 2);       p += 2;
            *p++ = entries[i].level;
            *p++ = entries[i].reserved;
            memcpy(p, &entries[i].timestamp, 4);  p += 4;
            memcpy(p, entries[i].msg, msg_len);

            cmd_frame_t push;
            push.cmd     = CMD_SYSTEM;
            push.sub     = cmd_frame_sub_rsp(CMD_SUB_LOG_SUBSCRIBE);
            push.len     = (uint16_t)pld_len;
            push.payload = pld;

            cmd_conn_send(conn, &push);
            free(pld);
        }

        /* 回复确认 */
        uint8_t ok = CMD_ERR_OK;
        cmd_frame_t ack = { .cmd = CMD_SYSTEM, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 1, .payload = &ok };
        cmd_conn_send(conn, &ack);

        LOG_DEBUG("Log subscription added, pushed %d buffered entries", count);

    } else if (op == CMD_SUB_LOG_UNSUBSCRIBE) {
        /* 取消日志订阅 */
        cmd_subscription_mgr_t* sub_mgr = (cmd_subscription_mgr_t*)ctx;

        if (sub_mgr) {
            cmd_subscription_remove(sub_mgr, CMD_DATA_LOG, conn);
        }

        uint8_t ok = CMD_ERR_OK;
        cmd_frame_t ack = { .cmd = CMD_SYSTEM, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 1, .payload = &ok };
        cmd_conn_send(conn, &ack);

        LOG_DEBUG("Log subscription removed");

    } else {
```

- [ ] **Step 2: Add missing includes at top of file**

```c
#include "cmd/cmd_subscription.h"  /* sub_mgr */
#include <arpa/inet.h>            /* htons */
```

- [ ] **Step 3: Verify compilation**

```bash
./build.sh
```

- [ ] **Step 4: Commit**

```bash
git add modules/cmd/src/cmd_handler_system.c
git commit -m "feat(cmd): add log subscribe/unsubscribe to system handler"
```

---

### Task 14: main.c — Wire Up Log Callback

**Files:**
- Modify: `user/main.c`

- [ ] **Step 1: Add `on_log_push` callback**

After `sensor_thread` and before `cleanup`:

```c
/* ── 日志推送回调 ─────────────────────────────────────────────────── */

/**
 * log 模块回调：将每条日志通过订阅管理器推送。
 */
static void on_log_push(uint8_t level, const char* msg, void* ctx)
{
    cmd_subscription_mgr_t* sub_mgr = (cmd_subscription_mgr_t*)ctx;
    if (!sub_mgr || !msg) return;

    /* 构造推送数据: [level 1B, reserved 1B, timestamp 4B LE, msg N B] */
    uint32_t ts = (uint32_t)time(NULL);
    size_t msg_len = strlen(msg);
    size_t data_len = 1 + 1 + 4 + msg_len;
    uint8_t data[1024];  /* 栈上分配，足够大多数日志 */
    uint8_t* buf = data;
    uint8_t* heap_buf = NULL;

    if (data_len > sizeof(data)) {
        heap_buf = (uint8_t*)malloc(data_len);
        if (!heap_buf) return;
        buf = heap_buf;
    }

    buf[0] = level;
    buf[1] = 0;  /* reserved */
    memcpy(buf + 2, &ts, 4);  /* LE timestamp */
    memcpy(buf + 6, msg, msg_len);

    cmd_subscription_push(sub_mgr, CMD_SYSTEM, CMD_DATA_LOG, buf, data_len);

    if (heap_buf) free(heap_buf);
}
```

- [ ] **Step 2: Register callback after log_init**

In `main()`, after `log_init(...)` and before hardware init:

```c
    /* 注册日志推送回调 */
    log_set_subscribe_callback(on_log_push, app_cmd_get_sub_mgr(g_cmd));
```

Note: this needs to happen AFTER `g_cmd = app_cmd_create()` since `sub_mgr` is created there. Move this line to after the `app_cmd_create()` call.

Actually, looking at the flow:
1. `log_init` — early in main
2. `sht30_open`, `led_open` — hardware init  
3. `app_cmd_create` — creates sub_mgr
4. Register handlers
5. Register listeners
6. `app_cmd_run`

The `log_set_subscribe_callback` should be called after step 3 (when sub_mgr exists). Place it right after `app_cmd_create()`:

```c
    g_cmd = app_cmd_create();
    if (!g_cmd) { ... return 1; }

    /* 注册日志推送回调（必须在 app_cmd_create 之后，因为需要 sub_mgr） */
    log_set_subscribe_callback(on_log_push, app_cmd_get_sub_mgr(g_cmd));
```

- [ ] **Step 3: Remove or keep the existing `cmd_handler_system` ctx change**

The `cmd_handler_system` registration currently passes `NULL` as ctx. But Task 13 needs `sub_mgr` as ctx for log subscribe/unsubscribe. Change:

```c
    app_cmd_register(g_cmd, CMD_SYSTEM, cmd_handler_system, NULL);
```

to:

```c
    app_cmd_register(g_cmd, CMD_SYSTEM, cmd_handler_system, app_cmd_get_sub_mgr(g_cmd));
```

- [ ] **Step 4: Verify compilation**

```bash
./build.sh
```

- [ ] **Step 5: Commit**

```bash
git add user/main.c
git commit -m "feat(main): wire log subscribe callback and system handler ctx"
```

---

## Phase 5: Tests

### Task 15: Protocol Unit Tests

**Files:**
- Create: `pc_dashboard/tests/__init__.py`
- Create: `pc_dashboard/tests/test_cmd_frame.py`

- [ ] **Step 1: Write test file**

```python
""" cmd_frame.py 单元测试 """

import struct
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../.."))

from pc_dashboard.protocol.cmd_frame import pack, parse, crc8, sub_rsp, sub_req
from pc_dashboard.protocol.cmd_defs import (
    CMD_LED, CMD_SENSOR, CMD_SYSTEM,
    CMD_SUB_WRITE, CMD_SUB_READ, CMD_SUB_SUBSCRIBE,
    CMD_SUB_RESPONSE_FLAG,
)

PASSED = 0
FAILED = 0


def test(name, fn):
    global PASSED, FAILED
    print(f"  TEST {name} ... ", end="", flush=True)
    try:
        fn()
        print("PASS")
        PASSED += 1
    except AssertionError as e:
        print(f"FAIL: {e}")
        FAILED += 1
    except Exception as e:
        print(f"ERROR: {e}")
        FAILED += 1


# ── 测试用例 ─────────────────────────────────────────────────

def test_roundtrip():
    """ 往返: pack → parse → 一致 """
    f = {"cmd": CMD_LED, "sub": CMD_SUB_WRITE, "len": 2, "payload": b"\x01\x01"}
    data = pack(f)
    assert data[0] == 0xA5 and data[1] == 0x5A, "magic mismatch"
    r, c = parse(data)
    assert r is not None, "parse returned None"
    assert r["cmd"] == CMD_LED
    assert r["sub"] == CMD_SUB_WRITE
    assert r["payload"] == b"\x01\x01"
    assert c == len(data)


def test_empty_payload():
    """ 空 payload """
    f = {"cmd": CMD_SYSTEM, "sub": CMD_SUB_READ, "len": 0, "payload": b""}
    data = pack(f)
    r, c = parse(data)
    assert r is not None
    assert r["payload"] == b""


def test_crc_mismatch():
    """ CRC 错误应跳过 """
    f = {"cmd": CMD_SENSOR, "sub": CMD_SUB_READ, "len": 0, "payload": b""}
    data = bytearray(pack(f))
    data[-1] ^= 0xFF  # 破坏 CRC
    r, c = parse(bytes(data))
    assert r is None, "CRC should fail"


def test_garbage_prefix():
    """ 前导垃圾字节应被跳过 """
    f = {"cmd": CMD_LED, "sub": CMD_SUB_WRITE, "len": 1, "payload": b"\x01"}
    data = b"\xFF\xEE\xDD" + pack(f)
    r, c = parse(data)
    assert r is not None
    assert c == len(data)


def test_split_frame():
    """ 分片到达：先收到部分帧头 """
    f = {"cmd": CMD_SYSTEM, "sub": CMD_SUB_READ, "len": 5, "payload": b"hello"}
    data = pack(f)

    # 只给前 4 字节（不够完整帧）
    r, c = parse(data[:4])
    assert r is None and c == 0, "should need more data"

    # 给完整帧
    r, c = parse(data)
    assert r is not None
    assert r["payload"] == b"hello"


def test_back_to_back():
    """ 两帧粘包 """
    f1 = {"cmd": CMD_LED, "sub": CMD_SUB_WRITE, "len": 1, "payload": b"\x01"}
    f2 = {"cmd": CMD_SENSOR, "sub": CMD_SUB_READ, "len": 0, "payload": b""}
    data = pack(f1) + pack(f2)

    r1, c1 = parse(data)
    assert r1 is not None and r1["cmd"] == CMD_LED

    r2, c2 = parse(data[c1:])
    assert r2 is not None and r2["cmd"] == CMD_SENSOR


def test_bigendian_len():
    """ LEN 字段 BE 序：65KB 边界内合法 """
    pl = b"x" * 300
    f = {"cmd": CMD_SYSTEM, "sub": CMD_SUB_READ, "len": len(pl), "payload": pl}
    data = pack(f)
    r, c = parse(data)
    assert r is not None
    assert r["len"] == 300
    assert len(r["payload"]) == 300


def test_sub_rsp():
    """ 响应位设置/清除 """
    assert sub_rsp(CMD_SUB_WRITE) == CMD_SUB_WRITE | 0x80
    assert sub_req(sub_rsp(CMD_SUB_READ)) == CMD_SUB_READ


def test_crc8_consistency():
    """ CRC8 与 C 端一致 """
    assert crc8(b"\xA5\x5A\x00\x00\x01\x02") == 0xA5 ^ 0x5A ^ 0x00 ^ 0x00 ^ 0x01 ^ 0x02


# ── 入口 ───────────────────────────────────────────────────

if __name__ == "__main__":
    print("=== cmd_frame tests ===\n")

    test("roundtrip", test_roundtrip)
    test("empty payload", test_empty_payload)
    test("CRC mismatch", test_crc_mismatch)
    test("garbage prefix", test_garbage_prefix)
    test("split frame", test_split_frame)
    test("back-to-back frames", test_back_to_back)
    test("big-endian LEN", test_bigendian_len)
    test("sub rsp/req", test_sub_rsp)
    test("CRC8 consistency", test_crc8_consistency)

    print(f"\n=== Results: {PASSED} passed, {FAILED} failed ===")
    sys.exit(0 if FAILED == 0 else 1)
```

- [ ] **Step 2: Run tests**

```bash
python3 pc_dashboard/tests/test_cmd_frame.py
```

Expected: 9/9 PASS

- [ ] **Step 3: Commit**

```bash
git add pc_dashboard/tests/
git commit -m "test(pc_dashboard): add protocol unit tests (9 cases)"
```

---

### Task 16: Integration Test

**Files:**
- Create: `pc_dashboard/tests/test_integration.py`

- [ ] **Step 1: Write integration test** (runs against real cmd_server on host)

```python
""" 集成测试 —— 启动 cmd_server + mock handler，验证 PC 端客户端通信 """

import sys
import os
import time
import struct
import subprocess
import threading

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../.."))

from pc_dashboard.protocol.cmd_client import CmdClient
from pc_dashboard.protocol.cmd_defs import (
    CMD_LED, CMD_SYSTEM,
    CMD_SUB_WRITE, CMD_SUB_READ,
    CMD_ERR_OK,
)

PASSED = 0
FAILED = 0


def test(name, fn):
    global PASSED, FAILED
    print(f"  TEST {name} ... ", end="", flush=True)
    try:
        fn()
        print("PASS")
        PASSED += 1
    except Exception as e:
        print(f"FAIL: {e}")
        FAILED += 1


# ── 测试 ────────────────────────────────────────────────────

def test_led_roundtrip():
    """ LED 读写往返 """
    client = CmdClient()
    assert client.connect("127.0.0.1", 19527), "connect failed"

    # 写 LED
    rsp = client.send_request(CMD_LED, CMD_SUB_WRITE, b"\x01\x01")
    assert rsp is not None, "no response for LED write"
    pl = rsp["payload"]
    assert pl[0] == CMD_ERR_OK, f"error code {pl[0]}"

    # 读 LED
    rsp = client.send_request(CMD_LED, CMD_SUB_READ, b"\x01")
    assert rsp is not None, "no response for LED read"
    assert rsp["payload"][0] == CMD_ERR_OK

    client.disconnect()


def test_system_info():
    """ 系统信息查询 """
    client = CmdClient()
    assert client.connect("127.0.0.1", 19527), "connect failed"

    rsp = client.send_request(CMD_SYSTEM, CMD_SUB_READ)
    assert rsp is not None
    assert rsp["payload"][0] == CMD_ERR_OK
    ver = bytes(rsp["payload"][1:]).decode()
    assert "1.0.0" in ver, f"unexpected version: {ver}"

    client.disconnect()


def test_concurrent_requests():
    """ 并发请求（3 个快速连续） """
    client = CmdClient()
    assert client.connect("127.0.0.1", 19527), "connect failed"

    results = []
    for i in range(3):
        rsp = client.send_request(CMD_SYSTEM, CMD_SUB_READ)
        assert rsp is not None, f"request {i} failed"
        results.append(rsp)

    for rsp in results:
        assert rsp["payload"][0] == CMD_ERR_OK

    client.disconnect()


if __name__ == "__main__":
    print("=== Integration Tests ===\n")
    print("(Requires test_cmd_integration running on port 19527)")
    print("Start with: cd build && ./test_cmd_integration\n")

    test("LED roundtrip", test_led_roundtrip)
    test("System info", test_system_info)
    test("Concurrent requests", test_concurrent_requests)

    print(f"\n=== Results: {PASSED} passed, {FAILED} failed ===")
    sys.exit(0 if FAILED == 0 else 1)
```

- [ ] **Step 2: Verify structure**

```bash
python3 -c "import pc_dashboard; print('Package OK')"
```

- [ ] **Step 3: Commit**

```bash
git add pc_dashboard/tests/test_integration.py
git commit -m "test(pc_dashboard): add integration test against cmd_server"
```
