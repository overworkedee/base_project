""" 集成测试 —— 启动 cmd_server + mock handler，验证 PC 端客户端通信 """

import sys
import os

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


def test_led_roundtrip():
    """ LED 读写往返 """
    client = CmdClient()
    assert client.connect("127.0.0.1", 19527), "connect failed"

    rsp = client.send_request(CMD_LED, CMD_SUB_WRITE, b"\x01\x01")
    assert rsp is not None, "no response for LED write"
    assert rsp["payload"][0] == CMD_ERR_OK, f"error code {rsp['payload'][0]}"

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

    for i in range(3):
        rsp = client.send_request(CMD_SYSTEM, CMD_SUB_READ)
        assert rsp is not None, f"request {i} failed"
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
