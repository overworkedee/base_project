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
        frame: {"cmd": int, "sub": int, "payload": bytes|None}

    返回:
        完整的二进制帧 bytes（含 HEAD + LEN + CMD + SUB + PAYLOAD + CRC）

    异常:
        ValueError: payload 长度超过 65535
    """
    payload = frame.get("payload") or b""
    payload_len = len(payload)

    if payload_len > CMD_FRAME_MAX_PAYLOAD:
        raise ValueError(f"payload too large: {payload_len} > 65535")

    # HEAD(2B) + LEN(2B BE) + CMD(1B) + SUB(1B) + PAYLOAD(N B)
    header = struct.pack(
        ">BB HB B",
        CMD_FRAME_HEAD_MAGIC,  # 0xA5
        CMD_FRAME_TAIL_MAGIC,  # 0x5A
        payload_len,            # LEN, big-endian uint16
        frame["cmd"],
        frame["sub"],
    )
    full = header + payload
    crc_byte = bytes([crc8(full)])
    return full + crc_byte


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
