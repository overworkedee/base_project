# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
# Full build (cross-compile for ARM64 RK3588)
./build.sh

# Pass additional CMake arguments
./build.sh -DCMAKE_BUILD_TYPE=Debug
```

The build script sources `env/rk3588_product_orangerpi5plus.env`, configures CMake, and compiles with `make -j$(nproc)`. Output binary is `build/hello_world`.

To switch toolchains, set `TOOLCHAIN_PATH` before running the build script:

```bash
export TOOLCHAIN_PATH=/path/to/other/toolchain
./build.sh
```

## Architecture

This is a cross-compiled C project targeting the **Orange Pi 5 Plus** (Rockchip RK3588 SoC, ARM64/aarch64), running Ubuntu 22.04 Server with kernel 6.1.43.

### Directory layout

- **`user/`** — Application source code. Entry point is `main.c`.
- **`part/`** — Third-party prebuilt libraries (`.so`/`.a`). Linked via `link_directories()` in CMakeLists.txt. Currently empty.
- **`env/`** — Platform environment files. Source one to set up `CROSS_COMPILE`, `PATH`, `ARCH`, and other build variables.
- **`toolchain`** — Symlink to the ARM cross-compilation toolchain. Note: the env file uses its own hardcoded `TOOLCHAIN_PATH` (`$HOME/orangepi-build/toolchains/...`) and does **not** reference this symlink. The symlink is for convenience/documentation only.
- **`build/`** — CMake build directory (auto-created).

### Build flow

1. `env/rk3588_product_orangerpi5plus.env` is sourced to set environment variables (`CROSS_COMPILE=aarch64-none-linux-gnu-`, `ARCH=arm64`, etc.)
2. `build.sh` clears CMake cache, runs `cmake ..`, then `make`
3. `CMakeLists.txt` reads `$CROSS_COMPILE` from the environment to select the cross-compiler; falls back to the host compiler if unset

### HW IO Framework

The `hw/` directory provides a hardware abstraction library (`libhw`), cross-compiled for RK3588.

**Modules:**
- `hw/src/bus/bus_i2c.c` — I2C bus driver (thread-safe, mutex-protected)
- `hw/src/dev/dev_led.c` — LED control via sysfs (`/sys/class/leds/<name>/`), supports on/off and heartbeat/none trigger
- `hw/src/hw_mutex.c` — pthread mutex wrapper
- `hw/src/hw_error.c` — unified error code strings

**HW Test Demo** — interactive CLI for board-level hardware testing:

```bash
# Build with demo enabled
cmake -DHW_BUILD_DEMO=ON ..
make -j$(nproc)

# Run the interactive test program
./hw/hw_demo
```

Demo commands (type `help` after launch for full list):
```
hw> led on          # Turn on blue_led
hw> led off         # Turn off blue_led
hw> led heartbeat   # Heartbeat blink
hw> i2c scan        # Scan /dev/i2c-0 through /dev/i2c-6
hw> quit            # Exit
```

### Log Module

项目自带日志模块（`liblog`），所有日志输出必须使用该模块，**禁止直接使用 printf/fprintf**。

```c
#include "log/log.h"

// 初始化（程序启动时调用一次）
log_init("/tmp/app.log", LOG_DEBUG);

// 按等级输出日志，自动捕获文件名/行号/函数名
LOG_DEBUG("variable x = %d", x);   // 调试，受 LOG_ENABLE_DEBUG 宏控制
LOG_INFO("service started");       // 正常运行信息
LOG_WARN("retry count: %d", n);    // 警告
LOG_ERROR("malloc failed");        // 错误

// 退出前调用
log_deinit();
```

**要点：**
- 线程安全，内部持有互斥锁
- `LOG_DEBUG` 受编译宏 `LOG_ENABLE_DEBUG`（env 文件）控制，关闭时编译为零开销
- 日志格式自动包含 `[等级 文件:行号 函数]` 前缀，无需手动拼

### Adding new sources

Add `.c` files to `user/` and list them in `CMakeLists.txt` under `add_executable()`. For additional libraries, place `.so`/`.a` files in `part/` and add `-l<name>` via `target_link_libraries()` in CMakeLists.txt.

## Coding Convention

所有函数使用中文注释，遵循统一格式：

```c
/**
 * 简短描述函数的功能（做什么，一句话）
 *
 * @param param1  参数1的含义和约束（可为NULL？取值范围？）
 * @param param2  参数2的含义和约束
 * @return        返回值含义（NULL表示什么？错误码含义？）
 * @note          特殊注意事项、副作用、线程安全性等（可选）
 */
hw_err_t some_function(int param1, void* param2);
```

要点：
- 每个 `.h` 中的公开函数必须有完整注释
- `.c` 中的内部函数至少写一行 `/* 内部：xxx */`
- 参数和返回值必须说清楚，不能用含糊描述
- `@note` 用于标注线程安全、可重入、副作用等关键信息
