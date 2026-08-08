/**
 * 视觉识别 Demo
 *
 * 纯 C 程序，通过 vision.h 的 C 接口调用 OpenCV，无需直接引用 C++ API。
 * 完整演示：加载 → 信息查询 → 灰度化 → 边缘检测 → 统计 → 保存 全流程。
 *
 * @note 运行: ./vision_demo [图片路径]，默认 /home/orangepi/photo.jpg
 */
#include "vision/vision.h"
#include <stdio.h>

int main(int argc, char* argv[])
{
    /* 确定输入路径 */
    const char* path = "/home/orangepi/photo.jpg";
    if (argc > 1) {
        path = argv[1];
    }

    printf("========== OpenCV Vision Demo ==========\n");
    printf("Loading: %s\n", path);

    /* 加载图像 */
    vision_image_t* img = vision_load(path);
    if (!img) {
        printf("ERROR: Failed to load image\n");
        return 1;
    }

    printf("Resolution : %d x %d\n", vision_width(img), vision_height(img));
    printf("Channels   : %d\n", vision_channels(img));

    /* 灰度化 */
    if (vision_to_gray(img) != 0) {
        printf("ERROR: Grayscale conversion failed\n");
        vision_destroy(img);
        return 1;
    }
    printf("Grayscale OK.\n");

    /* 灰度统计 */
    double min_val, max_val;
    vision_min_max(img, &min_val, &max_val);
    printf("Gray range : [%.0f, %.0f]\n", min_val, max_val);

    /* Canny 边缘检测 */
    if (vision_canny(img, 50, 150) != 0) {
        printf("ERROR: Canny failed\n");
        vision_destroy(img);
        return 1;
    }
    printf("Canny edge detection OK.\n");

    /* 保存结果 */
    char out_path[256];
    snprintf(out_path, sizeof(out_path), "%s_edges.jpg", path);
    if (vision_save(img, out_path) != 0) {
        printf("ERROR: Failed to save\n");
        vision_destroy(img);
        return 1;
    }
    printf("Saved: %s\n", out_path);

    /* 边缘像素统计 */
    int edge_pixels;
    vision_count_nonzero(img, &edge_pixels);
    printf("Edge pixels: %d (%.1f%%)\n",
           edge_pixels,
           100.0 * edge_pixels / (vision_width(img) * vision_height(img)));

    vision_destroy(img);
    printf("==========================================\n");
    return 0;
}
