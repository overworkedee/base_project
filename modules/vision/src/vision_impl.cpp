/**
 * 视觉识别模块 —— C++ 实现
 *
 * 对上层暴露纯 C 接口，内部使用 OpenCV C++ API。
 * 所有函数通过 opaque pointer (vision_image_t) 封装 cv::Mat，
 * 调用方无需包含任何 C++ 头文件。
 */
#include "vision/vision.h"
#include <opencv2/opencv.hpp>

struct vision_image {
    cv::Mat mat;
};

/* ── 生命周期 ─────────────────────────────────────────────────── */

/**
 * 从文件加载图像，返回不透明句柄。
 *
 * @param path  图像文件路径（JPEG/PNG/BMP 等），不可为 NULL
 * @return      成功返回 vision_image_t*，失败（文件不存在/格式不支持）返回 NULL
 */
vision_image_t* vision_load(const char* path)
{
    if (!path) return nullptr;

    cv::Mat m = cv::imread(path);
    if (m.empty()) return nullptr;

    vision_image_t* img = new vision_image_t();
    img->mat = m;
    return img;
}

/**
 * 释放图像句柄及其占用的内存。
 *
 * @param img  vision_load 返回的句柄，可为 NULL（无操作）
 * @note       调用后 img 指针失效，不得再使用
 */
void vision_destroy(vision_image_t* img)
{
    delete img;
}

/* ── 内存构造 ─────────────────────────────────────────────────── */

/**
 * 从内存中的 NV12 帧构造图像（转 BGR），供摄像头拍照流水线使用。
 *
 * NV12 布局：前 height 行存放 Y 平面，随后 height/2 行存放
 * UV 交错平面（每行 width 字节，行间可能有 stride 填充）。
 * 构造时自动去除行填充，最终图像为 BGR 三通道。
 *
 * @param data     NV12 帧数据起始地址（Y 平面首字节），不可为 NULL
 * @param width    帧宽（像素），须为偶数
 * @param height   帧高（像素），须为偶数
 * @param stride   每行字节数（bytesperline），与 width 相等时无行填充
 * @return         成功返回 vision_image_t*（BGR），失败返回 NULL
 * @note           返回的句柄由调用者通过 vision_destroy 释放；
 *                 data 指向的缓冲区由调用者管理，本函数会拷贝数据
 */
vision_image_t* vision_create_from_nv12(const void* data, int width, int height,
                                        int stride)
{
    if (!data || width <= 0 || height <= 0 || stride < width) return nullptr;

    cv::Mat nv12;
    if (stride == width) {
        /* 无行填充：整块 NV12 直接构造成 3h/2 行矩阵 */
        nv12 = cv::Mat(height * 3 / 2, width, CV_8UC1, (void*)data);
    } else {
        /* 有行填充：逐行拷贝去除 stride 空隙 */
        const uint8_t* p = (const uint8_t*)data;
        nv12 = cv::Mat(height * 3 / 2, width, CV_8UC1);
        for (int row = 0; row < height * 3 / 2; row++) {
            memcpy(nv12.ptr<uint8_t>(row), p + (size_t)row * stride, (size_t)width);
        }
    }

    cv::Mat bgr;
    cv::cvtColor(nv12, bgr, cv::COLOR_YUV2BGR_NV12);

    vision_image_t* img = new vision_image_t();
    img->mat = bgr;
    return img;
}

/* ── 图像信息 ─────────────────────────────────────────────────── */

/**
 * 获取图像宽度（像素）。
 *
 * @param img  有效图像句柄，为 NULL 时返回 0
 * @return     宽度（列数）
 */
int vision_width(const vision_image_t* img)
{
    return img ? img->mat.cols : 0;
}

/**
 * 获取图像高度（像素）。
 *
 * @param img  有效图像句柄，为 NULL 时返回 0
 * @return     高度（行数）
 */
int vision_height(const vision_image_t* img)
{
    return img ? img->mat.rows : 0;
}

/**
 * 获取图像通道数。
 *
 * @param img  有效图像句柄，为 NULL 时返回 0
 * @return     1=灰度，3=BGR 彩色，4=BGRA
 */
int vision_channels(const vision_image_t* img)
{
    return img ? img->mat.channels() : 0;
}

/* ── 图像处理（原地修改） ────────────────────────────────────── */

/**
 * 将图像原地转换为灰度图。
 *
 * @param img  有效图像句柄，不可为 NULL
 * @return     0 成功，-1 失败（img 无效或图像为空）
 * @note       若已是单通道灰度图则直接返回 0，不触发转换
 */
int vision_to_gray(vision_image_t* img)
{
    if (!img || img->mat.empty()) return -1;

    if (img->mat.channels() == 1) return 0;

    cv::Mat gray;
    cv::cvtColor(img->mat, gray, cv::COLOR_BGR2GRAY);
    img->mat = gray;
    return 0;
}

/**
 * 对图像做 Canny 边缘检测，结果原地覆盖原图。
 *
 * @param img          有效图像句柄，不可为 NULL
 * @param low_thresh   低阈值（典型值 50）
 * @param high_thresh  高阈值（典型值 150）
 * @return             0 成功，-1 失败
 * @note               内部自动先将彩图转灰度再检测，结果变为单通道二值边缘图
 */
int vision_canny(vision_image_t* img, double low_thresh, double high_thresh)
{
    if (!img || img->mat.empty()) return -1;

    cv::Mat gray;
    if (img->mat.channels() == 1) {
        gray = img->mat;
    } else {
        cv::cvtColor(img->mat, gray, cv::COLOR_BGR2GRAY);
    }

    cv::Mat edges;
    cv::Canny(gray, edges, low_thresh, high_thresh);
    img->mat = edges;
    return 0;
}

/* ── I/O ──────────────────────────────────────────────────────── */

/**
 * 将图像保存到文件。
 *
 * @param img   有效图像句柄，不可为 NULL
 * @param path  输出文件路径（根据后缀自动选择编码器），不可为 NULL
 * @return      0 成功，-1 失败（路径不可写或 img 无效）
 */
int vision_save(const vision_image_t* img, const char* path)
{
    if (!img || img->mat.empty() || !path) return -1;

    return cv::imwrite(path, img->mat) ? 0 : -1;
}

/* ── 图像统计 ─────────────────────────────────────────────────── */

/**
 * 获取图像像素的最小值和最大值。
 *
 * @param img      有效图像句柄，不可为 NULL
 * @param min_val  输出参数：最小值，不可为 NULL
 * @param max_val  输出参数：最大值，不可为 NULL
 * @return         0 成功，-1 失败
 */
int vision_min_max(const vision_image_t* img, double* min_val, double* max_val)
{
    if (!img || img->mat.empty() || !min_val || !max_val) return -1;

    cv::minMaxLoc(img->mat, min_val, max_val);
    return 0;
}

/**
 * 统计图像中非零像素个数。
 *
 * @param img    有效图像句柄，不可为 NULL
 * @param count  输出参数：非零像素数，不可为 NULL
 * @return       0 成功，-1 失败
 */
int vision_count_nonzero(const vision_image_t* img, int* count)
{
    if (!img || img->mat.empty() || !count) return -1;

    *count = cv::countNonZero(img->mat);
    return 0;
}
