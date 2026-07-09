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

# 列出所有编译产物
echo ""
echo "=== Build artifacts ==="
for f in $PROJECT_DIR/out/bin/*; do
    if [ -f "$f" ]; then
        file "$f"
    fi
done

# 7. Deploy to Orange Pi
echo ""
echo "==> Deploying to Orange Pi..."

# SCP 所有可执行文件
for f in $PROJECT_DIR/out/bin/*; do
    if [ -f "$f" ] && [ -x "$f" ]; then
        echo "  scp $f ..."
        scp "$f" orangepi@192.168.3.171:~
    fi
done

echo "==> Deploy complete."
