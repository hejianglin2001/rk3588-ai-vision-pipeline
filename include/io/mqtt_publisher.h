#pragma once
#include "mosquitto.h"
#include <string>
#include <functional>
#include <cstdio>
#include <cstring>

// ================================================================
//  MQTT 发送器 — 把 JSON 推送到笔记本
//
//  用法三步:
//    1. mqtt_init("topeet", "192.168.1.100")   // 连接 broker
//    2. mqtt_send(json_string)                  // 发一帧数据
//    3. mqtt_cleanup()                          // 退出时清理
//
//  笔记本上先启动 broker:  mosquitto -p 1883
//  然后订阅看消息:          mosquitto_sub -t robot/perception
// ================================================================

// ── 消息成功发出的回调 (mosquitto 内部调用, 你不用管) ──
static void on_publish(struct mosquitto* mosq, void* userdata, int mid) {
    // mid = 消息编号, 发成功了
    // 不打印, 每帧都发太吵
}

// ── 初始化, 连接 MQTT Broker ──
// client_id: 板端名字, 随便起 (如 "rk3588")
// broker_ip: 笔记本IP地址 (如 "192.168.1.100")
// port:      默认 1883
// 返回: 连接句柄, nullptr 表示失败
inline struct mosquitto* mqtt_init(const char* client_id,
                                     const char* broker_ip,
                                     int port = 1883) {
    // 初始化 mosquitto 库 (只需调一次, 重复调也没事)
    mosquitto_lib_init();

    // 创建一个客户端实例
    struct mosquitto* mosq = mosquitto_new(client_id, true, nullptr);
    if (!mosq) {
        fprintf(stderr, "❌ [MQTT] mosquitto_new 失败\n");
        return nullptr;
    }

    // 注册发送成功的回调
    mosquitto_publish_callback_set(mosq, on_publish);

    // 连接 broker
    int ret = mosquitto_connect(mosq, broker_ip, port, 60);
    if (ret != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "❌ [MQTT] 连接 broker %s:%d 失败, 错误码=%d\n",
                broker_ip, port, ret);
        mosquitto_destroy(mosq);
        return nullptr;
    }

    printf("✅ [MQTT] 已连接到 %s:%d\n", broker_ip, port);
    return mosq;
}

// ── 发送一帧 JSON 到笔记本 ──
// mosq:    mqtt_init 返回的句柄
// topic:   主题名, 如 "robot/perception"
// json:    boxes_to_json() 生成的字符串
inline void mqtt_send(struct mosquitto* mosq,
                       const char* topic,
                       const std::string& json) {
    if (!mosq) return;  // 没连上就跳过

    int ret = mosquitto_publish(mosq, nullptr, topic,
                                 json.size(), json.c_str(),
                                 0,     // qos=0: 发出去就行, 不确认
                                 false);// retain=false: 不存最后一条
    if (ret != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "❌ [MQTT] 发送失败: %s\n", mosquitto_strerror(ret));
    }
}

// ── 接收指令 (订阅主题 + 非阻塞收信) ──
static std::function<void(const std::string&)> g_on_command;  // 回调函数

// mosquitto 收到消息时调这个
static void _mqtt_on_message(struct mosquitto*, void*, const struct mosquitto_message* msg) {
    std::string payload((char*)msg->payload, msg->payloadlen);
    if (g_on_command) g_on_command(payload);
}

// 订阅主题, 设置回调: 收到消息时自动调 callback
inline void mqtt_subscribe(struct mosquitto* mosq, const char* topic,
                            std::function<void(const std::string&)> callback) {
    g_on_command = callback;
    mosquitto_message_callback_set(mosq, _mqtt_on_message);
    mosquitto_subscribe(mosq, nullptr, topic, 0);
    printf("📡 [MQTT] 订阅: %s\n", topic);
}

// 每帧调用一次: 查收件箱 (timeout=0ms, 不阻塞)
inline void mqtt_check_mail(struct mosquitto* mosq) {
    if (mosq) mosquitto_loop(mosq, 0, 1);
}

// ── 清理 ──
inline void mqtt_cleanup(struct mosquitto* mosq) {
    if (mosq) {
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
    }
    mosquitto_lib_cleanup();
}
