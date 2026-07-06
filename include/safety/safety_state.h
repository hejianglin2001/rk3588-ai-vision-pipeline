#pragma once
#include "core/postprocess.h"
#include <cstdio>
#include <cstring>

// ================================================================
//  安全兜底状态机 — 端侧最后一道防线
//  任何 LLM 指令必须经过这里检查, 不通过就刹车
// ================================================================

// ── LLM 下发的动作 ──
enum class Action {
    MOVE_FORWARD,    // 前进
    MOVE_BACKWARD,   // 后退
    TURN_LEFT,       // 左转
    TURN_RIGHT,      // 右转
    GRAB,            // 抓取
    STOP,            // 停止
    NONE             // 无指令
};

// ── 检查结果 ──
struct SafetyResult {
    bool safe;             // true=放行, false=拦截
    char reason[128];      // 拦截原因 (给LLM解释用)
    char status[32];       // "normal" / "emergency_stop" / "disconnected"
};

// ── 安全检查: 每帧调用, 输入检测框 + 当前指令 ──
// connected = 网络是否通, 断网时不信任任何LLM指令
inline SafetyResult safety_check(
    const std::vector<Box>& boxes,
    Action action,
    int img_w = 640,
    int img_h = 640,
    bool connected = true)
{
    SafetyResult r;
    memset(&r, 0, sizeof(r));

    // 规则1: 断网 → 本地避障, 拒绝所有LLM指令
    if (!connected) {
        r.safe = false;
        snprintf(r.reason, sizeof(r.reason),
                 "网络断开: 已拒绝LLM指令, 切换至本地视觉避障模式");
        snprintf(r.status, sizeof(r.status), "disconnected");
        return r;
    }

    // 规则2: 前进时检查前方有没有障碍物
    // 真正的障碍特征: 大 + 底部贴近画面下边缘 (物体就在眼前)
    if (action == Action::MOVE_FORWARD) {
        for (const auto& b : boxes) {
            float area   = (b.x2 - b.x1) * (b.y2 - b.y1);
            float ratio  = area / (img_w * img_h);
            float cx     = (b.x1 + b.x2) / 2.0f;
            float bottom = b.y2;                     // bbox 底部Y坐标

            bool in_front = (cx > img_w * 0.15f &&   // 不在画面极边缘
                             cx < img_w * 0.85f);
            bool near     = (bottom > img_h * 0.85f); // 底部触达画面下边缘(近在眼前)
            bool blocking = (ratio > 0.40f);           // 占画面 40%+

            if (near && blocking && in_front) {
                r.safe = false;
                snprintf(r.reason, sizeof(r.reason),
                         "前方障碍物: 物体贴近(底部%.0f%%, 占比%.0f%%), 紧急刹车!",
                         bottom/img_h*100, ratio*100);
                snprintf(r.status, sizeof(r.status), "emergency_stop");
                return r;
            }
        }
    }

    // 规则3: 没有检测到物体时不能抓取
    if (action == Action::GRAB && boxes.empty()) {
        r.safe = false;
        snprintf(r.reason, sizeof(r.reason), "视野内无物体, 无法执行抓取动作");
        snprintf(r.status, sizeof(r.status), "emergency_stop");
        return r;
    }

    // 放行
    r.safe = true;
    snprintf(r.reason, sizeof(r.reason), "前方安全, 可以通行");
    snprintf(r.status, sizeof(r.status), "normal");
    return r;
}
