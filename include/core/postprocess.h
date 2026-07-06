#pragma once
#include <vector>

struct Box {
    float x1, y1, x2, y2;
    float conf;
    int   cls;
};

// 原版: 单个 [84,8400] 数组
std::vector<Box> postprocess(const float* output, float conf_thres=0.45f, float iou_thres=0.45f);

// 拆分版: bbox[4,8400] + score[80,8400] → 省掉拼接 memcpy
std::vector<Box> postprocess_split(const float* bbox, const float* score,
                                    float conf_thres=0.45f, float iou_thres=0.45f);
