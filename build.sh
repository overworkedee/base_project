#!/bin/bash
set -e

PROJECT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR=$PROJECT_DIR/build

# 1. Load environment
echo "==> Loading environment..."
source $PROJECT_DIR/env/rk3588_product_orangerpi5plus.env

# 2. Prepare build directory
mkdir -p $BUILD_DIR
cd $BUILD_DIR

# 3. Clear stale cache (ensure cross compiler is re-detected)
echo "==> Clearing cache..."
rm -rf CMakeCache.txt CMakeFiles/

# 4. CMake configure
echo "==> Configuring..."
cmake .. $@

# 5. Clean
echo "==> Cleaning..."
make clean 2>/dev/null || true

# 6. Build
echo "==> Building..."
make -j$(nproc)

echo "==> Done."
file $PROJECT_DIR/out/bin/project_app
