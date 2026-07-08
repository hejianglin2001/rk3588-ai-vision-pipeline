#pragma once
// ╔══════════════════════════════════════════════════════════╗
// ║  全局配置文件 — 改参数只改这里, 不用翻 main.cpp        ║
// ╚══════════════════════════════════════════════════════════╝

// ── 摄像头 ──
#define CAM_DEVICE      "/dev/video11"
#define CAM_WIDTH       1920
#define CAM_HEIGHT      1080

// ── 模型输入 ──
#define MODEL_W          640
#define MODEL_H          640
#define MODEL_C          3
#define MODEL_BYTES      (MODEL_W * MODEL_H * MODEL_C)  // 1,228,800

// ── 网络 ──
#define MQTT_BROKER_IP  "192.168.31.164"
#define MQTT_PORT        1883
#define MQTT_TOPIC_CMD   "robot/command"
#define MQTT_TOPIC_PERF  "robot/perf"
#define BOARD_IP         "192.168.31.63"
#define RTSP_PORT        8554

// 字符串化宏 (#x 把整数值变成字符串 "整数值")
#define CFG_TOSTR_(x) #x
#define CFG_TOSTR(x)  CFG_TOSTR_(x)
#define RTSP_URL      "rtsp://" BOARD_IP ":" CFG_TOSTR(RTSP_PORT) "/live"

// ── 管线参数 ──
#define QUEUE_SIZE       4       // 线程间队列深度 (背压阈值)
#define PERF_INTERVAL    30      // 性能报告间隔 (帧数)
#define FPS_EMA_ALPHA    0.1     // FPS 指数平滑系数

// ── RTSP 推流 ──
#define STREAM_PIPE      "/tmp/video_pipe"

// ── 功能开关 (1=开, 0=关) ──
#define USE_RGA          1     // RGA 硬件预处理 (UYVY→RGB+缩放)
#define USE_MULTITHREAD  0     // 三线程管线
#define USE_RTSP         1    // RTSP 推流
#define USE_ZCOPY        0     // RKNN 零拷贝输入
#define USE_VIDEO            0     // 1=循环读图片测FPS上限, 0=摄像头
#define VIDEO_PATH           "./images/000000000785.jpg"
