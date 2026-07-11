""" TCP 命令客户端 —— 连接管理 + 后台线程收帧。

请求-响应采用串行模式（一次只发一个请求），避免与板端 handler
payload 格式冲突。订阅推送在后台线程通过 push_callback 分发。"""

import socket
import struct
import threading
from typing import Optional, Callable

from .cmd_frame import pack, parse
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
        """
        连接到开发板的 cmd_server。

        自动断开旧连接、创建 socket、启动后台读取线程。

        @param host  IP 地址或主机名
        @param port  TCP 端口（默认 9527）
        @return     True 连接成功，False 失败
        """
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
        """
        断开连接，停止后台读取线程，释放 socket 资源。

        安全操作：可对已断开的连接调用。
        """
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

            frame = {"cmd": cmd, "sub": sub, "payload": payload}

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
                    print("CmdClient: connection closed by peer")
                    self._on_connection_lost()
                    break

                buf.extend(chunk)

                # 循环解析缓冲区中的所有帧
                while True:
                    frame, consumed = parse(bytes(buf))
                    if frame is None:
                        if consumed > 0:
                            del buf[:consumed]
                            continue
                        else:
                            break

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
        is_response = (frame["sub"] & 0x80) != 0

        if is_response:
            if not self._resp_event.is_set():
                # 有请求在等待——这是它的响应
                self._response = frame
                self._resp_event.set()
            elif self._push_callback:
                # 推送帧（订阅数据、日志流等）
                self._push_callback(frame)
        # bit7=0 的请求帧：客户端不应收到，忽略
