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
