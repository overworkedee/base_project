# OpenCV 视觉模块指南

## 模块概览

`modules/vision` 提供纯 C 接口的 OpenCV 封装，调用方无需引入任何 C++ 头文件。
底层基于 OpenCV 4.x 静态库（ARM64 交叉编译），通过 opaque pointer 模式隐藏 C++ 类型。

### 目录结构

```
modules/vision/
├── CMakeLists.txt              # 静态库构建（vision），自动传递所有 OpenCV 依赖
├── include/vision/vision.h     # 纯 C 头文件，声明不透明接口
└── src/vision_impl.cpp         # C++ 实现，封装 cv::Mat 操作
```

### CMake 链接

```cmake
# 调用方只需链接 vision，OpenCV 依赖自动传递
target_link_libraries(your_target PRIVATE vision)
```

---

## C API 速览

| 函数 | 功能 | 返回 |
|------|------|------|
| `vision_load(path)` | 加载图片文件 | NULL=失败 |
| `vision_destroy(img)` | 释放图像内存 | - |
| `vision_width(img)` | 图像宽度（cols） | 像素数 |
| `vision_height(img)` | 图像高度（rows） | 像素数 |
| `vision_channels(img)` | 通道数（1/3/4） | 通道数 |
| `vision_to_gray(img)` | 原地转灰度 | 0=成功 |
| `vision_canny(img, lo, hi)` | 原地边缘检测 | 0=成功 |
| `vision_save(img, path)` | 保存到文件 | 0=成功 |
| `vision_min_max(img, &min, &max)` | 像素范围 | 0=成功 |
| `vision_count_nonzero(img, &n)` | 非零像素数 | 0=成功 |

**注意**：`_to_gray` / `_canny` 为**原地修改**，结果覆盖原始图像数据。

---

## OpenCV 核心 API 对比

### 1. 图像 I/O

```cpp
// 加载（支持 JPEG/PNG/BMP/WebP 等）
cv::Mat img = cv::imread("/path/to/photo.jpg");
// 返回值 cv::Mat，失败时 img.empty() == true

// 保存（根据后缀自动选编码器）
cv::imwrite("/path/to/output.png", img);
// 返回 true/false

// JPEG 质量参数
std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 90};
cv::imwrite("out.jpg", img, params);
```

### 2. 图像属性

```cpp
int w = img.cols;        // 宽度（列数）
int h = img.rows;        // 高度（行数）
int c = img.channels();  // 1=灰度, 3=BGR, 4=BGRA
int t = img.type();      // CV_8UC3 等（深度+通道组合）
```

**OpenCV 默认使用 BGR 顺序**，不是 RGB。

### 3. 颜色转换

```cpp
cv::Mat gray;
cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);  // BGR → 灰度
cv::cvtColor(img, gray, cv::COLOR_BGR2RGB);   // BGR → RGB
cv::cvtColor(img, hsv, cv::COLOR_BGR2HSV);    // BGR → HSV
```

常见转换码：`cv::COLOR_BGR2GRAY`、`cv::COLOR_BGR2RGB`、`cv::COLOR_BGR2HSV`、

### 4. 边缘检测（Canny）

```cpp
cv::Mat edges;
cv::Canny(gray, edges, low_thresh, high_thresh);
// low_thresh  典型值 50  — 低于此值的梯度直接判定为非边缘
// high_thresh 典型值 150 — 高于此值的梯度直接判定为边缘，之间看连通性

// 注意：输入必须是单通道灰度图，彩色图需先 cv::cvtColor
```

输出是 CV_8UC1 二值图，边缘像素=255，背景=0。

### 5. 像素统计

```cpp
double minVal, maxVal;
cv::minMaxLoc(img, &minVal, &maxVal);
// 灰度图返回 [0.0, 255.0]，Canny 结果返回 [0.0, 255.0]

int nonZeroPixels = cv::countNonZero(img);
// 边缘检测后用于计算边缘占比
```

---

## C/C++ 互操作模式（Opaque Pointer）

### .h — 声明不透明类型

```c
typedef struct vision_image vision_image_t;  // 仅声明，不暴露内部

vision_image_t* vision_load(const char* path);
void            vision_destroy(vision_image_t* img);
```

### .cpp — 内部用 C++ 实现

```cpp
struct vision_image {
    cv::Mat mat;           // C++ 对象藏在 struct 里
};

extern "C" vision_image_t* vision_load(const char* path) {
    auto* img = new vision_image_t();
    img->mat = cv::imread(path);
    return img;
}
```

**关键点**：
- 头文件纯 C，无需 `#include <opencv2/...>`
- 调用方只能用 `gcc` 编译，不需要 `g++`
- 析构在 `extern "C"` 函数中调用 `delete`，确保 C++ 析构正确执行

---

## 静态库链接注意事项

### OpenCV 模块间有环形依赖

`libopencv_world.a` 和 kleidicv 系列库之间有未解析符号的循环引用。

**解决方案**：用 `--start-group` / `--end-group` 包裹

```cmake
target_link_libraries(vision PRIVATE
    -Wl,--start-group
    ${OPENCV_LIBS}
    -Wl,--end-group
    ${OPENCV_SYS_LIBS}        # -lpthread -ldl -lm -lz 等
)
```

`--start-group` 告诉连接器对这些库做多轮扫描，解决循环依赖。

### 链接顺序

```
vision → --start-group <所有 opencv .a> --end-group → -lpthread -ldl -lm -lz
```

`--start-group` 内顺序无关紧要，连接器会反复解析直到稳定。

---

## 交叉编译要点

回忆本次交叉编译 OpenCV 的关键步骤：

1. 编写 `toolchain-arm64.cmake`（指定 CROSS_COMPILE、sysroot 等）
2. `cmake -DCMAKE_TOOLCHAIN_FILE=toolchain-arm64.cmake -DBUILD_SHARED_LIBS=OFF ...`
3. 启用 NEON/DOTPROD/FP16/BF16（RK3588 Architecture: ARMv8.2-a+sve+crypto）
4. 产物 `.a` 统一放到 `part/libs/`，头文件放到 `part/opencv-arm64/include/opencv4/`
5. 编译 OpenCV 的脚本保存在 `part/build_opencv_arm64.sh`

重新编译的命令：

```bash
cd part/opencv-4.x
./build_opencv_arm64.sh
```

---

## 后续可扩展功能

- `vision_resize` — 缩放（cv::resize）
- `vision_blur` — 高斯模糊（cv::GaussianBlur）
- `vision_threshold` — 二值化（cv::threshold）
- `vision_morphology` — 形态学操作（膨胀/腐蚀/开闭运算）
- `vision_find_contours` — 轮廓检测（cv::findContours）— **充电插座定位的关键步骤**
- `vision_draw_contours` — 画轮廓（cv::drawContours）
- `vision_rectangle_detect` — 矩形检测 — 插座孔定位
- `vision_hough_circles` — 圆检测（cv::HoughCircles）— 圆孔插座定位
- `vision_undistort` — 畸变校正 — 相机标定的前提

---

## 参考资料

- OpenCV 4.x 官方文档：https://docs.opencv.org/4.x/
- OpenCV 交叉编译指南：https://docs.opencv.org/4.x/d0/d76/tutorial_arm_crosscompile_with_cmake.html
- `part/build_opencv_arm64.sh` — 本次交叉编译脚本
- `part/opencv-4.x/toolchain-arm64.cmake` — 工具链文件
