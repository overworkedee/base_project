# Log 日志模块设计

## Overview

`modules/log/` — 通用日志模块，编译为独立静态库 `liblog.a`。提供 debug/info/warn/error 四级日志，支持文件+终端双输出，多线程安全，debug 等级可通过 env 文件编译期裁剪。

## Architecture

```
modules/
├── CMakeLists.txt              # 顶层：add_subdirectory(log)
└── log/
    ├── CMakeLists.txt           # 编译 liblog.a + demo
    ├── include/
    │   └── log/
    │       └── log.h            # 公开接口（宏 + 等级枚举 + 初始化/反初始化）
    ├── src/
    │   └── log.c                # 内部实现（格式化 + 写文件 + 互斥锁）
    ├── tests/
    │   └── test_log.c           # 单元测试
    └── demo/
        └── log_demo.c           # 使用示例
```

## Interface

### 日志等级

```c
typedef enum {
    LOG_DEBUG = 0,   /* 调试信息，仅开发期使用           */
    LOG_INFO  = 1,   /* 正常运行信息                     */
    LOG_WARN  = 2,   /* 警告，不影响运行但值得关注       */
    LOG_ERROR = 3,   /* 错误，功能受损但程序可继续       */
} log_level_t;
```

### 公开宏

```c
#define LOG_DEBUG(fmt, ...)  LOG_WRITE(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)   LOG_WRITE(LOG_INFO,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)   LOG_WRITE(LOG_WARN,  fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)  LOG_WRITE(LOG_ERROR, fmt, ##__VA_ARGS__)
```

自动捕获 `__FILE__`、`__LINE__`、`__func__`。

### 初始化 / 反初始化

```c
hw_err_t log_init(const char* file_path, log_level_t min_level);
void     log_set_level(log_level_t level);
void     log_deinit(void);
```

`log_init` 必须在 `LOG_*` 宏使用前调用；`log_deinit` 在程序退出前调用，刷新缓冲区并关闭文件。

### 输出格式

```
[2026-06-27 14:32:05.123] [INFO ] [main.c:42 main] i2c transfer done, addr=0x42
```

## Implementation Details

### 线程安全

日志模块内部持有 `pthread_mutex_t`（经 `hw/hw_mutex.h` 封装），`log_write_impl` 入口加锁、出口解锁，保证多线程日志不交叉。

### 运行时等级过滤

低于 `log_init` 或 `log_set_level` 设定的等级，直接 return 不输出。

### 编译期 DEBUG 裁剪

env 文件中定义：

```bash
# env/rk3588_product_orangerpi5plus.env
export LOG_ENABLE_DEBUG=1   # 1=启用DEBUG日志, 0=关闭
```

顶层 `CMakeLists.txt` 统一处理：

```cmake
if(DEFINED ENV{LOG_ENABLE_DEBUG} AND $ENV{LOG_ENABLE_DEBUG} EQUAL 0)
    add_definitions(-DLOG_ENABLE_DEBUG=0)
else()
    add_definitions(-DLOG_ENABLE_DEBUG=1)
endif()
```

`LOG_ENABLE_DEBUG=0` 时，`LOG_DEBUG` 宏展开为 `((void)0)`，编译后零开销。

## Build Integration

顶层 `CMakeLists.txt` 添加：

```cmake
add_subdirectory(${PROJECT_SOURCE_DIR}/modules)
```

用户链接：

```cmake
target_link_libraries(xxx log)
```

## Coding Convention

所有函数使用统一中文注释格式，包含 `@param`、`@return`、`@note` 标注。
