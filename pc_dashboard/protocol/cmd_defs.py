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
