#include "core/postprocess.h"
#include <algorithm>

// 两个框的重叠率
static float iou(const Box& a, const Box& b) {
    float x1 = std::max(a.x1, b.x1);
    float y1 = std::max(a.y1, b.y1);
    float x2 = std::min(a.x2, b.x2);
    float y2 = std::min(a.y2, b.y2);
    if (x1 >= x2 || y1 >= y2) return 0.0f;
    float inter = (x2 - x1) * (y2 - y1);
    float area_a = (a.x2 - a.x1) * (a.y2 - a.y1);
    float area_b = (b.x2 - b.x1) * (b.y2 - b.y1);
    return inter / (area_a + area_b - inter + 0.000001f);
}

std::vector<Box> postprocess(const float* output, float conf_thres, float iou_thres) {
    constexpr int NUM_CLASSES = 80;
    constexpr int NUM_ANCHORS = 8400;
    constexpr int IMG_W = 640;
    constexpr int IMG_H = 640;

    // ── 第1步: 置信度过滤 ──
    std::vector<Box> boxes;
    for (int a = 0; a < NUM_ANCHORS; ++a) {
        // 找80个类别里分数最高的
        float best_conf = 0.0f;
        int   best_cls  = 0;
        for (int c = 0; c < NUM_CLASSES; ++c) {
            float s = output[(4 + c) * NUM_ANCHORS + a];
            if (s > best_conf) { best_conf = s; best_cls = c; }
        }
        if (best_conf < conf_thres) continue;

        // 读 bbox: cx,cy,w,h (已经是像素坐标, 不需要乘)
        float cx = output[0 * NUM_ANCHORS + a];
        float cy = output[1 * NUM_ANCHORS + a];
        float w  = output[2 * NUM_ANCHORS + a];
        float h  = output[3 * NUM_ANCHORS + a];

        // 直接转 xyxy (坐标已在像素空间)
        float x1 = cx - w / 2.0f;
        float y1 = cy - h / 2.0f;
        float x2 = cx + w / 2.0f;
        float y2 = cy + h / 2.0f;

        // 裁剪到图像边界
        x1 = std::max(0.0f, std::min(x1, (float)IMG_W));
        y1 = std::max(0.0f, std::min(y1, (float)IMG_H));
        x2 = std::max(0.0f, std::min(x2, (float)IMG_W));
        y2 = std::max(0.0f, std::min(y2, (float)IMG_H));

        if (x2 - x1 < 2.0f || y2 - y1 < 2.0f) continue;
        boxes.push_back({x1, y1, x2, y2, best_conf, best_cls});
    }

    // ── 第2步: 按置信度排序 ──
    std::sort(boxes.begin(), boxes.end(),
              [](const Box& a, const Box& b) { return a.conf > b.conf; });

    // ── 第3步: NMS去重 (同类+高重叠 → 干掉) ──
    std::vector<bool> keep(boxes.size(), true);
    std::vector<Box> result;
    for (size_t i = 0; i < boxes.size(); ++i) {
        if (!keep[i]) continue;
        result.push_back(boxes[i]);
        for (size_t j = i + 1; j < boxes.size(); ++j) {
            if (!keep[j]) continue;
            if (boxes[i].cls == boxes[j].cls && iou(boxes[i], boxes[j]) > iou_thres)
                keep[j] = false;
        }
    }
    return result;
}

// 拆分版: bbox[4,8400] + score[80,8400] — 省掉拼接 memcpy
std::vector<Box> postprocess_split(const float* bbox, const float* score,
                                   float conf_thres, float iou_thres) {
    constexpr int NC = 80, NA = 8400, W = 640, H = 640;

    std::vector<Box> boxes;
    boxes.reserve(256);

    for (int a = 0; a < NA; ++a) {
        float best_conf = 0.0f;
        int   best_cls  = 0;
        for (int c = 0; c < NC; ++c) {
            float s = score[c * NA + a];
            if (s > best_conf) { best_conf = s; best_cls = c; }
        }
        if (best_conf < conf_thres) continue;

        float cx = bbox[0 * NA + a];
        float cy = bbox[1 * NA + a];
        float w  = bbox[2 * NA + a];
        float h  = bbox[3 * NA + a];

        float x1 = cx - w / 2.0f;
        float y1 = cy - h / 2.0f;
        float x2 = cx + w / 2.0f;
        float y2 = cy + h / 2.0f;

        x1 = std::max(0.0f, std::min(x1, (float)W));
        y1 = std::max(0.0f, std::min(y1, (float)H));
        x2 = std::max(0.0f, std::min(x2, (float)W));
        y2 = std::max(0.0f, std::min(y2, (float)H));
        if (x2 - x1 < 2.0f || y2 - y1 < 2.0f) continue;

        boxes.push_back({x1, y1, x2, y2, best_conf, best_cls});
    }

    std::sort(boxes.begin(), boxes.end(),
              [](const Box& a, const Box& b) { return a.conf > b.conf; });

    std::vector<bool> keep(boxes.size(), true);
    std::vector<Box> result;
    result.reserve(boxes.size());
    for (size_t i = 0; i < boxes.size(); ++i) {
        if (!keep[i]) continue;
        result.push_back(boxes[i]);
        for (size_t j = i + 1; j < boxes.size(); ++j) {
            if (!keep[j]) continue;
            if (boxes[i].cls == boxes[j].cls && iou(boxes[i], boxes[j]) > iou_thres)
                keep[j] = false;
        }
    }
    return result;
}
