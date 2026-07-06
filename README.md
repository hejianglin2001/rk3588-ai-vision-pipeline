# YOLO26n-RKNN-Deploy

[![Platform](https://img.shields.io/badge/platform-RK3588-orange)](https://www.rock-chips.com/)
[![Language](https://img.shields.io/badge/language-C%2B%2B17-blue)]()

纯 C++ RK3588 端侧实时目标检测管线——V4L2 摄像头采集 → RGA 硬件预处理 → INT8 NPU 推理 → MPP 硬编码 → RTSP 推流。

---

## 系统架构

```
┌────────────────── RK3588 ──────────────────────┐       ┌─ 笔记本 ───────────┐
│                                                 │       │                    │
│  MIPI 摄像头 (IMX415)                           │       │  MQTT Broker        │
│    │ UYVY 1920×1080                             │       │    │               │
│    ▼                                            │       │    ├─ perceiption  │
│  V4L2 采集 (手写 Multiplanar)                    │       │    └─ perf log     │
│    │                                            │       │                    │
│    ▼                                            │       │  VLC / ffplay      │
│  RGA 硬件预处理                                  │       │    │               │
│  UYVY→RGB + 1080→640 (imresize, ~1ms)           │       │    rtsp://         │
│    │                                            │       │                    │
│    ▼                                            │       └────────────────────┘
│  NPU 推理 (YOLO26n, INT8, 25ms)                        ▲         ▲
│    │ 0 号输出: bbox [4×8400]                     │       │         │
│    ├ 1 号输出: score [80×8400]                  │       │        MQTT
│    ▼                                            │       │
│  NMS 后处理 → 检测框                             │       │
│    │                                            │       │
│    ├── 安全状态机 (硬实时, 不依赖网络)              │       │
│    ├── MQTT 上报 (JSON) ─────────────────────────┘       │
│    └── 可视化 → NV12 → FIFO                       │       │
│                            │                      │       │
│                            ▼                      │       │
│                      GStreamer pipeline:           │       │
│                      videoparse → mpph264enc      │       │
│                      → h264parse → tcpserversink   │       │
│                            │                      │       │
│                            ▼                      │       │
│                      ffmpeg: H264 TCP → RTSP       │       │
│                            │                      │       │
│                            ▼                      │       │
│                      mediamtx: RTSP Server (:8554) │       │
│                            │                      │       │
│                      rtsp://板子IP:8554/live ──────┘       │
└─────────────────────────────────────────────────────────────┘
```

## 硬件与性能

| 项目 | 说明 |
|------|------|
| **芯片** | RK3588 (3×NPU, 1×RGA, 1×VPU, 4-ch LPDDR5) |
| **摄像头** | IMX415 MIPI, 1920×1080, UYVY |
| **模型** | YOLO26n, 80类, 640×640, INT8 (ONNX 图手术) |
| **最优 FPS** | **27.5** (RGA + 单线程 + RTSP) |
| **NPU** | ~23ms (INT8, 较FP16降50%) |
| **采集** | 1.9ms (RGA, 较CPU降70%) |

### 消融实验 (8 配置, 摄像头实测, ZCOPY=0)

| # | RGA | MT | RTSP | FPS | capt | NPU | NMS |
|---|-----|----|------|-----|------|-----|-----|
| 1 | 0 | 0 | 0 | 30.1 | 5.8 | 24.2 | 3.2 |
| 2 | 0 | 0 | 1 | 28.5 | 6.3 | 24.3 | 3.4 |
| 3 | 0 | 1 | 0 | 28.0 | 22.0 | 25.1 | 7.2 |
| 4 | 0 | 1 | 1 | 29.8 | 8.2 | 24.9 | 8.9 |
| **5** | **1** | **0** | **1** | **26.1** | **3.6** | **23.5** | **4.4** |
| 6 | 1 | 0 | 0 | 20.4 | 7.1 | 25.7 | 16.5 |
| 7 | 1 | 1 | 0 | 30.1 | 33.3 | 24.3 | 3.5 |
| 8 | 1 | 1 | 1 | 23.9 | 4.2 | 25.9 | 16.1 |

> 最优配置 **#5: RGA+ST+RTSP**, 采集仅 3.6ms, FPS 26.1 稳定可预测。多线程(#7, #8)受 DDR 控制器仲裁影响, 各阶段波动剧烈。

### 核心发现: DDR 带宽是零和游戏

```
capture 和 NMS 严格互斥 —— 不可能同时快:

RGA+MT(配置7):    capt=33.3ms  NMS=3.5ms   ← CPU采集被NPU挤掉, NMS吃饱
RGA+MT(配置8):    capt=4.2ms   NMS=16.1ms  ← RGA DMA抢赢, NMS饿死
RGA+ST(配置5):    capt=3.6ms   NMS=4.4ms   ← 串行, 各自独占DDR
```

**开了多线程后，总有一个阶段在 DDR 排队。RK3588 的 DDR 子系统（4-ch LPDDR5, 峰值 34GB/s）虽然带宽充裕，但多个 DMA 主控（RGA/NPU/CPU/VPU）同时访问时，DDR 控制器按优先级和 bank 冲突调度请求，导致各模块互相等待。单线程各模块独占控制器，稳定且可预测。**

### INT8 量化效果

| 版本 | FPS | capt | NPU | NMS |
|------|-----|------|-----|-----|
| FP16 (未量化) | 18.5 | 15.0 | 50.5 | 3.3 |
| **INT8 (量化)** | **29.8** | 17.8 | **24.9** | 8.7 |
| **变化** | **+61%** | +19% | **-51%** | +164% |

> NPU 耗时减半 (50→25ms)，FPS 跃升 61%。这是本项目最大的单次性能跳变。

![摄像头FPS](docs/images/perf_fps_camera.png)
![摄像头耗时](docs/images/perf_latency_camera.png)
![图片循环FPS](docs/images/perf_fps_imgloop.png)
![DDR争抢](docs/images/perf_ddr_scatter.png)
![RGAxMT](docs/images/perf_rga_mt.png)
![量化对比](docs/images/perf_quant.png)

## 快速开始

### 1. 编译 (交叉编译)

```bash
# PC 端 (需要 arm64 交叉编译器)
cd convert_cpp
./build-linux.sh

# 推送到板端
scp -r build root@<rk3588-ip>:~/yolo_deploy/
adb push ./build  /home/topeet/code/build/
```

### 2. 板端首次部署

```bash
# 下载 mediamtx (RTSP 服务端)
mkdir -p ~/yolo_deploy/tools && cd ~/yolo_deploy/tools
wget https://github.com/bluenviron/mediamtx/releases/download/v1.11.3/mediamtx_v1.11.3_linux_arm64v8.tar.gz
tar xzf mediamtx_v*.tar.gz && rm mediamtx_v*.tar.gz
cat > mediamtx.yml << 'EOF'
rtspAddress: :8554
rtspTransports: [udp, tcp]
paths:
  live:
    source: publisher
EOF
```

### 3. 运行

```bash
cd ~/yolo_deploy
./start_rtsp.sh                    # 一键启动 (模型/RTSP/采集)

# PC 端观看
vlc rtsp://192.168.0.101:8554/live
# 或
ffplay -fflags nobuffer -flags low_delay rtsp://192.168.0.101:8554/live
```

### 4. MQTT 日志 (PC端)

```bash
cd tools && g++ -std=c++17 mqtt_logger.cpp -lmosquitto -o mqtt_logger
./mqtt_logger 192.168.0.104

# 自动生成 perf_rga1_mt0_rtsp1.txt 等文件
```

## 功能开关

所有开关在 [include/config.h](include/config.h)：

```c
#define USE_RGA          1     // RGA 硬件预处理 (1=开, 0=CPU fallback)
#define USE_MULTITHREAD  0     // 三线程管线 (1=多线程, 0=单线程)
#define USE_RTSP         1     // RTSP 推流 (1=推, 0=不推)
#define USE_ZCOPY        1     // RKNN 零拷贝输入
#define USE_VIDEO        0     // 图片循环测试模式
```

改一个宏，重新编译即可跑 A/B 对比。

## 目录结构

```
convert_cpp/
├── src/main.cpp                    # 主程序 (~300行, 管线调度)
├── include/
│   ├── config.h                    # 全局配置 + 功能开关
│   ├── core/
│   │   ├── pipeline.h              # 线程安全队列 SafeQueue<T>
│   │   ├── preprocess.h            # CPU 预处理
│   │   └── postprocess.h           # NMS 后处理
│   ├── io/
│   │   ├── camera_capture.h        # V4L2 采集 (Multiplanar)
│   │   ├── mqtt_publisher.h        # MQTT 通信 (mosquitto)
│   │   ├── json_output.h           # 结构化 JSON
│   │   ├── overlay.h               # 画面叠加 + NV12 推流
│   │   └── coco_names.h            # COCO 80 类名
│   ├── hw/
│   │   ├── rga_preprocess.h        # RGA 硬件缩放+转色
│   │   └── mpp_encoder.h           # MPP 硬编码
│   └── safety/
│       └── safety_state.h          # 安全状态机
├── process/
│   ├── preprocess.cpp              # 预处理实现
│   └── postprocess.cpp             # NMS 实现
├── tools/
│   ├── mqtt_logger.cpp             # MQTT 日志接收器
│   ├── test_rga.cpp                # RGA 最小测试 (4级逐级验证)
│   └── mediamtx.yml                # RTSP 服务端配置
├── start_rtsp.sh                   # 一键启动
├── stop_rtsp.sh                    # 一键停止
├── CMakeLists.txt                  # CMake (交叉编译)
├── build-linux.sh                  # 一键编译+打包
└── docs/
    └── 项目开发全记录.md              # 完整踩坑记录 + 问题解决
```

## 技术点

- **ONNX 图手术**: 拆分输出层, bbox 和 class score 各自独立 INT8 量化 scale, NPU 50ms→25ms
- **RGA 硬件加速**: posix_memalign 页对齐 + mlock 锁内存解决 DMA get_user_pages 失败, capture 18ms→3.6ms
- **V4L2 Multiplanar**: 直连 ISP 输出, 跳过 OpenCV/libv4l2 中间层, 零额外拷贝
- **MPP + RTSP**: 4 进程链路 (C++→FIFO→GStreamer→ffmpeg→mediamtx), MPP 硬编码零 CPU 开销
- **DDR 争抢分析**: 9 配置全链路对比, 定位 RK3588 DDR 总线为系统瓶颈




## 截图1
![RTSP推流](docs/images/rtsp-vlc.png)

## 截图2
![性能终端](docs/images/terminal-perf.png)


## License

MIT
