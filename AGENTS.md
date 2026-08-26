# AGENTS.md

This file provides guidance for OpenCode when working with code in this repository.

**交流语言：所有对话使用中文。**

详细的架构、模块、规范说明见 [CLAUDE.md](./CLAUDE.md)，本文件仅补充 OpenCode 特有的操作指引。

## Build

```bash
# Full cross-compile (ARM64 RK3588), then auto-deploy to dev board via scp
./build.sh

# Pass CMake args
./build.sh -DCMAKE_BUILD_TYPE=Debug

# Enable tests + demos (KEY=VALUE before ./build.sh)
BUILD_TESTS=1 HW_BUILD_DEMO=1 ./build.sh
```

构建脚本自动 `source env/rk3588_product_orangerpi5plus.env`，编译后自动 `scp` 产物到 `orangepi@192.168.3.171:~/`。如果你的开发板 IP 不同，修改 `build.sh` 末尾的 scp 行。

## Tests

测试仅在 **host 编译** 时启用（CMakeLists 中有 `NOT CMAKE_CROSSCOMPILING` 守卫）。交叉编译时 `BUILD_TESTS=1` 仅编译 hw 层测试，cmd/log 测试会被跳过。

```bash
# Host 编译运行测试（需要先安装 GCC for x86_64）
rm -rf build && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
BUILD_TESTS=1 make
ctest --output-on-failure
```

## Project structure (beyond CLAUDE.md)

```
pc_dashboard/       # PySide6 Python 上位机，通过 TCP 连接开发板 cmd 模块
                    # pip install -r pc_dashboard/requirements.txt
                    # python3 pc_dashboard/main.py
modules/cmd/        # 命令模块（5层架构），详见 modules/cmd/GUIDE.md
```

## Coding Convention

所有函数使用中文注释。**注释写在实现文件（`.c`/`.cpp`）中，不写在头文件**，方便阅读源码。

- **`.c`/`.cpp` 中公开函数**：用完整 `/** */` 格式，必须包含 `@param`、`@return`，`@note` 可选
- **`.c`/`.cpp` 中内部函数（static/未在头文件声明）**：至少一行 `/* 内部：xxx */`
- **`.h` 中只保留分组横线和概要说明**，不写每函数注释

```c
// ✅ .h 中：只声明，不加注释
hw_err_t some_function(int param1, void* param2);

// ✅ .c 中：完整注释
/**
 * 简短描述函数的功能（做什么，一句话）
 *
 * @param param1  参数1的含义和约束（可为NULL？取值范围？）
 * @param param2  参数2的含义和约束
 * @return        返回值含义（NULL表示什么？错误码含义？）
 * @note          特殊注意事项、副作用、线程安全性等（可选）
 */
hw_err_t some_function(int param1, void* param2)
{
    /* ... */
}
```

## App Layer Convention

`user/` 是应用层（组装根 + 业务封装），遵循以下约定：

- **每个业务功能一个 `app_<feature>.c/.h`**，如 `app_sensor.c`、`app_led.c`。`.h` 只暴露不透明类型（`app_<feature>_t`）和函数声明
- **`main.c` 只是组装根**：只做 create → register → run（+atexit 清理），不写业务逻辑、不定义业务线程
- **业务线程/采集循环进 app 文件内部**（static），通过 `app_<feature>_start/stop` 暴露生命周期
- **命令 handler（`cmd_handler_*`）放在 `user/` 对应 app 文件**，通过 `app_cmd_svc_t` 能力表（`user/app_registry.h`）注入 cmd dispatcher——各 app 模块在 `app_<feature>_create()` 内自注册，main 只负责组装；cmd 模块本身不含任何业务 handler
- **Demo 一律独立可执行文件**（如 `vision_demo.c`），不塞进 `project_app`
- **新文件必须手动注册**：`user/` 下的 `.c` 加入顶层 `CMakeLists.txt` 的 `add_executable(project_app ...)`

**注册机制原理详见 [docs/registry.md](docs/registry.md)**（能力表结构、注册时序、推送流程、清理顺序、扩展示例）。

## Module Documentation

**每新增一个模块，必须在 `docs/` 下创建对应的 `.md` 指南文件**，记录：
- 模块概览和目录结构
- API 速览表
- 底层库/框架的用法要点
- 链接注意事项
- 后续可扩展方向

格式参考 `docs/opencv.md`。

## Gotchas

- **Toolchain path is hardcoded** in `env/rk3588_product_orangerpi5plus.env` (`$HOME/orangepi-build/toolchains/...`)。换机器时需改 env 文件或设置 `export TOOLCHAIN_PATH=...`。
- **Cleanup ordering in main.c** is critical：log callback 必须在 cmd module 销毁前注销，否则 LOG_* 会触发 use-after-free。
- **Log messages must be English**，注释使用中文。禁止 printf/fprintf，所有输出走 `LOG_*` 宏。`LOG_DEBUG` 受 `LOG_ENABLE_DEBUG` 编译宏控制（env 文件），关闭时零开销裁剪。
- **New `.c` files** 需在顶层 `CMakeLists.txt` 的 `add_executable()` 中注册。第三方 `.so/.a` 放入 `part/` 并在 `target_link_libraries()` 中添加 `-l<name>`。
- **第三方库规则**：能交叉编译的库下载到 `part/` 编译链接；无法交叉编译的系统运行时库，在开发板上用 `apt` 安装。
- **涉及构建流程/编译选项/环境变量/输出路径的改动，必须同步更新 README.md**。
- **build.sh 末尾的 scp** 会尝试连接 `orangepi@192.168.3.171`，如果目标不可达 build 仍然成功但部署步骤会失败。
