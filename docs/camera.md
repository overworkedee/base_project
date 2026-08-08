# 相机模块指南（dev_camera + app_camera + RTSP）

## 1. 模块概览

Orange Pi 5 Plus (RK3588) 相机子系统分三层：

```
┌─ 应用层 ─────────────────────────────────────────────────────┐
│  user/app_camera.c    拍照命令 + RTSP 推流进程管理            │
├─ 硬件层 ─────────────────────────────────────────────────────┤
│  hw/dev/dev_camera.c  标准 V4L2 采集驱动（mmap 单帧抓取）     │
├─ 系统层 ─────────────────────────────────────────────────────┤
│  rkisp0 (ISP) + OV13855 (13MP MIPI CSI)                     │
│    ├─ /dev/video-camera0 → rkisp_mainpath（拍照，高分辨率）  │
│    └─ /dev/video*        rkisp_selfpath（视频流，可缩放）    │
│  gst-rtsp-launch 工具进程（板上 apt 安装）                   │
└──────────────────────────────────────────────────────────────┘
```

## 2. 目录结构

```
hw/include/hw/dev/dev_camera.h    V4L2 采集驱动头（只声明）
hw/src/dev/dev_camera.c           V4L2 采集实现
user/app_camera.h/.c              相机应用模块（命令 + RTSP 管理）
modules/cmd/include/cmd/cmd_frame.h  CMD_CAMERA 命令号定义
```

## 3. API 速览

### dev_camera（hw 层）

| 函数 | 说明 |
|------|------|
| `camera_open(path)` | 打开 V4L2 设备（自动识别 MPLANE/单平面） |
| `camera_find_by_card(substr, ...)` | 按卡片名（如 `rkisp_mainpath`）动态查找节点，解决设备号漂移 |
| `camera_set_format(cam, w, h, fourcc)` | 设置分辨率与像素格式，回读实际 stride |
| `camera_start(cam)` | 请求 4 个 mmap 缓冲并 STREAMON |
| `camera_grab(cam, dst, cap, size)` | 阻塞抓一帧，拷贝到调用者缓冲，自动重新入队 |
| `camera_stop(cam)` / `camera_close(cam)` | 停止采集 / 关闭释放 |

### app_camera（应用层）

| 命令 | 说明 |
|------|------|
| `camera snap [WxH]` | 拍照：抓 NV12 帧 → vision 转 BGR → 灰度/Canny → 统计非零像素 → 存 `/tmp/camera_*.jpg` |
| `camera status` | 查询 busy 状态与 RTSP 运行状态 |
| `camera rtsp on/off` | 启停 RTSP 推流进程 |

## 4. 底层要点

### rkisp 管道（内核侧，无需用户干预）

- media0 = rkcif（CSI 原始接口），media1 = rkisp（ISP 处理）
- `rkisp-isp-subdev` 同一帧流分发到 mainpath / selfpath / fbcpath，三者链路均 ENABLED，**可并行**
- mainpath：全分辨率（最高 4224x3136），用于拍照
- selfpath：最高 1920x3136，带缩放，用于 720p 视频流
- 节点号在重启后可能漂移 → 用 `camera_find_by_card("rkisp_mainpath")` 查找

### V4L2 采集要点

- rkisp 是 **MPLANE** 设备（`V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE`），但 NV12 只有一个 plane
- 帧大小（NV12，4:2:0）：`stride × height × 3 / 2`
- **stride 注意**：rkisp 按 32 字节对齐，`stride >= width`，取帧后用 `vision_create_from_nv12(frame, w, h, stride)` 自动去填充
- 同一 V4L2 节点只能有一个打开者：拍照（mainpath）与 RTSP（selfpath）必须用不同节点

### RTSP 管道

```
v4l2src device=<selfpath> ! video/x-raw,format=NV12,
width=1280,height=720,framerate=30/1 !
mpph264enc bitrate=3000000 ! rtph264pay name=pay0 pt=96
```

- `mpph264enc`：Rockchip MPP 硬件 H264 编码器（gst-rockchipmpp 插件）
- 通过 `rtsp-launch` 工具进程启动（`tools/rtsp_launch.c`，板上 gcc 编译），监听 8554 端口
- 子进程 stdout/stderr 重定向到 `/tmp/rtsp_launch.log` 便于排查
- 上位机拉流：`ffplay rtsp://192.168.3.171:8554/` 或 OpenCV `VideoCapture("rtsp://...")`

### RTSP 工具安装（板上一次性）

Ubuntu 仓库的 `gstreamer1.0-rtsp` 包只含库，不含 test-launch 二进制，
需在板上编译本项目自带的工具：

```bash
# 板上（需已安装开发环境）
sudo apt install libgstrtspserver-1.0-dev   # 头文件 + .pc（1.20 已默认装）
gcc tools/rtsp_launch.c -o rtsp-launch \
    $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-server-1.0)
sudo cp rtsp-launch /usr/local/bin/
```

注意 pkg-config 包名为 `gstreamer-rtsp-server-1.0`（不是 gst-rtsp-server）。

## 5. 链接注意事项

- dev_camera 仅依赖标准 Linux V4L2 头（`<linux/videodev2.h>`），无第三方库
- vision 模块提供 `vision_create_from_nv12` 内存接口（.cpp 内用 OpenCV `cvtColor` NV12→BGR）
- RTSP 工具进程依赖板载 gstreamer（`apt install gstreamer1.0-rtsp`），交叉编译无需 gstreamer 头文件

## 6. 验证方法

```bash
# 1. 确认设备
ls /dev/video-camera0; v4l2-ctl -d /dev/video-camera0 --list-formats-ext

# 2. 直接验证 V4L2 采集（板上）
gst-launch-1.0 v4l2src device=/dev/video-camera0 \
  ! video/x-raw,format=NV12,width=1280,height=720,framerate=30/1 ! fakesink

# 3. 启动应用（project_app 自动拉起 RTSP）
./project_app
# 日志: /tmp/project.log, /tmp/rtsp_launch.log

# 4. 上位机拉流
ffplay rtsp://192.168.3.171:8554/

# 5. 拍照（cmd_demo）
./cmd_demo; connect unix; camera snap; camera status
```

## 7. 后续可扩展方向

- pc_dashboard 增加 RTSP 播放面板（OpenCV VideoCapture 拉流）
- H264 编码参数（码率/关键帧间隔）参数化到 env 文件
- 拍照分辨率由上位机动态指定（当前支持 `camera snap WxH`）
- 采集线程周期抓帧做持续识别（当前仅命令触发单帧）
