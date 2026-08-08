#!/bin/bash
# OpenCV 交叉编译脚本 — 目标 RK3588 (ARM64)，产物 static .a
# 使用项目的 aarch64-none-linux-gnu- 工具链
# 编译结果安装到 ../opencv-arm64/，.a 文件同步到 ../libs/

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$SCRIPT_DIR/opencv-4.x"
BUILD_DIR="$SRC_DIR/build-arm"
INSTALL_DIR="$SCRIPT_DIR/opencv-arm64"
LIBS_DIR="$SCRIPT_DIR/libs"
TOOLCHAIN_FILE="$SRC_DIR/toolchain-arm64.cmake"

# 确保 part/libs/ 存在
mkdir -p "$LIBS_DIR"

echo "==> Cleaning old build..."
rm -rf "$BUILD_DIR" "$INSTALL_DIR"
mkdir -p "$BUILD_DIR" "$INSTALL_DIR"

echo "==> Configuring OpenCV (ARM64, static, NEON)..."
cmake "$SRC_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_opencv_world=ON \
  -DOPENCV_FORCE_3RDPARTY_BUILD=ON \
  -DCPU_BASELINE=NEON \
  -DWITH_OPENCL=OFF \
  -DWITH_OPENMP=OFF \
  -DWITH_GTK=OFF \
  -DWITH_QT=OFF \
  -DWITH_FFMPEG=OFF \
  -DWITH_GSTREAMER=OFF \
  -DWITH_1394=OFF \
  -DWITH_VTK=OFF \
  -DWITH_OPENEXR=ON \
  -DBUILD_opencv_dnn=OFF \
  -DBUILD_opencv_python=OFF \
  -DBUILD_opencv_java=OFF \
  -DBUILD_opencv_js=OFF \
  -DBUILD_opencv_ts=OFF \
  -DBUILD_opencv_apps=OFF \
  -DBUILD_opencv_highgui=ON \
  -DBUILD_TESTS=OFF \
  -DBUILD_PERF_TESTS=OFF \
  -DBUILD_EXAMPLES=OFF \
  -DENABLE_CCACHE=OFF

echo "==> Building..."
make -j6

echo "==> Installing..."
make install

echo "==> Collecting .a files to $LIBS_DIR ..."
find "$INSTALL_DIR" -name "*.a" -exec cp -v {} "$LIBS_DIR/" \;

echo "==> Done. Libraries in: $LIBS_DIR"
echo "    Headers in:     $INSTALL_DIR/include/opencv4/"
