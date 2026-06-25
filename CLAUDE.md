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

### Adding new sources

Add `.c` files to `user/` and list them in `CMakeLists.txt` under `add_executable()`. For additional libraries, place `.so`/`.a` files in `part/` and add `-l<name>` via `target_link_libraries()` in CMakeLists.txt.
