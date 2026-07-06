#pragma once
#include "core/postprocess.h"
#include "io/coco_names.h"
#include <string>
#include <cstdio>
#include <cstring>
#include <sys/time.h>

// ── 障碍物信息 (深度估计接入后填真实值) ──
struct ObstacleInfo {
    bool   has_obstacle       = false;
    float  nearest_distance_m = 0.0f;
    char   direction[16]      = "none";  // "front" / "left" / "right"
};

// ── 系统状态枚举 ──
enum class SystemStatus {
    NORMAL,
    DISCONNECTED,
    EMERGENCY_STOP
};

inline const char* status_str(SystemStatus s) {
    switch (s) {
        case SystemStatus::NORMAL:          return "normal";
        case SystemStatus::DISCONNECTED:    return "disconnected";
        case SystemStatus::EMERGENCY_STOP:  return "emergency_stop";
    }
    return "unknown";
}

// ── 生成完整结构化 JSON ──
inline std::string boxes_to_json(
    const std::vector<Box>& boxes,
    int frame_id              = 0,
    float fps                 = 0.0f,
    int img_w                 = 640,
    int img_h                 = 640,
    const ObstacleInfo& obs   = ObstacleInfo{},
    SystemStatus status       = SystemStatus::NORMAL)
{
    // 时间戳 (毫秒)
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    long long ts = (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    char buf[512];
    std::string json = "{\n";

    // ── 帧头信息 ──
    snprintf(buf, sizeof(buf),
        "  \"timestamp\": %lld,\n"
        "  \"frame_id\": %d,\n"
        "  \"resolution\": {\"w\": %d, \"h\": %d},\n"
        "  \"fps\": %.1f,\n",
        ts, frame_id, img_w, img_h, fps);
    json += buf;

    // ── 检测物体列表 ──
    json += "  \"objects\": [\n";
    for (size_t i = 0; i < boxes.size(); ++i) {
        const auto& b = boxes[i];
        const char* name = (b.cls >= 0 && b.cls < 80) ? COCO_NAMES[b.cls] : "unknown";
        float cx = (b.x1 + b.x2) / 2.0f;   // 2D 中心点
        float cy = (b.y1 + b.y2) / 2.0f;

        snprintf(buf, sizeof(buf),
            "    {\n"
            "      \"id\": %zu,\n"
            "      \"class\": \"%s\",\n"
            "      \"conf\": %.2f,\n"
            "      \"bbox_2d\": {\"x1\": %.0f, \"y1\": %.0f, \"x2\": %.0f, \"y2\": %.0f},\n"
            "      \"center_2d\": {\"x\": %.0f, \"y\": %.0f},\n"
            "      \"distance_m\": null,\n"
            "      \"position_3d\": null\n"
            "    }",
            i + 1, name, b.conf,
            b.x1, b.y1, b.x2, b.y2,
            cx, cy);
        json += buf;
        if (i + 1 < boxes.size()) json += ",";
        json += "\n";
    }
    json += "  ],\n";

    // ── 障碍物信息 ──
    snprintf(buf, sizeof(buf),
        "  \"obstacle\": {\n"
        "    \"has_obstacle\": %s,\n"
        "    \"nearest_distance_m\": %s,\n"
        "    \"direction\": \"%s\"\n"
        "  },\n",
        obs.has_obstacle ? "true" : "false",
        obs.has_obstacle ? std::to_string(obs.nearest_distance_m).c_str() : "null",
        obs.direction);
    json += buf;

    // ── 系统状态 ──
    snprintf(buf, sizeof(buf),
        "  \"status\": \"%s\"\n"
        "}\n",
        status_str(status));
    json += buf;

    return json;
}
