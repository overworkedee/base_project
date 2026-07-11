""" cmd_frame.py 单元测试 """

import struct
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../.."))

from pc_dashboard.protocol.cmd_frame import pack, parse, crc8, sub_rsp, sub_req
from pc_dashboard.protocol.cmd_defs import (
    CMD_LED, CMD_SENSOR, CMD_SYSTEM,
    CMD_SUB_WRITE, CMD_SUB_READ,
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


def test_roundtrip():
    """ 往返: pack → parse → 一致 """
    f = {"cmd": CMD_LED, "sub": CMD_SUB_WRITE, "payload": b"\x01\x01"}
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
    f = {"cmd": CMD_SYSTEM, "sub": CMD_SUB_READ, "payload": b""}
    data = pack(f)
    r, c = parse(data)
    assert r is not None
    assert r["payload"] == b""


def test_crc_mismatch():
    """ CRC 错误应跳过 """
    f = {"cmd": CMD_SENSOR, "sub": CMD_SUB_READ, "payload": b""}
    data = bytearray(pack(f))
    data[-1] ^= 0xFF
    r, c = parse(bytes(data))
    assert r is None, "CRC should fail"


def test_garbage_prefix():
    """ 前导垃圾字节应被跳过 """
    f = {"cmd": CMD_LED, "sub": CMD_SUB_WRITE, "payload": b"\x01"}
    data = b"\xFF\xEE\xDD" + pack(f)
    r, c = parse(data)
    assert r is not None
    assert c == len(data)


def test_split_frame():
    """ 分片到达 """
    f = {"cmd": CMD_SYSTEM, "sub": CMD_SUB_READ, "payload": b"hello"}
    data = pack(f)
    r, c = parse(data[:4])
    assert r is None and c == 0, "should need more data"
    r, c = parse(data)
    assert r is not None
    assert r["payload"] == b"hello"


def test_back_to_back():
    """ 两帧粘包 """
    f1 = {"cmd": CMD_LED, "sub": CMD_SUB_WRITE, "payload": b"\x01"}
    f2 = {"cmd": CMD_SENSOR, "sub": CMD_SUB_READ, "payload": b""}
    data = pack(f1) + pack(f2)
    r1, c1 = parse(data)
    assert r1 is not None and r1["cmd"] == CMD_LED
    r2, c2 = parse(data[c1:])
    assert r2 is not None and r2["cmd"] == CMD_SENSOR


def test_bigendian_len():
    """ LEN 字段 BE 序 """
    pl = b"x" * 300
    f = {"cmd": CMD_SYSTEM, "sub": CMD_SUB_READ, "payload": pl}
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
