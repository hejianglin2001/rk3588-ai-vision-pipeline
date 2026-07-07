#!/bin/bash
# 一键部署: MQTT + 编译 + 推送 + 观看
set -e

echo -e "listener 1883 0.0.0.0\nallow_anonymous true" > /tmp/mqtt.conf
sudo systemctl stop mosquitto 2>/dev/null || true
sudo pkill mosquitto 2>/dev/null || true
sleep 0.3
mosquitto -c /tmp/mqtt.conf &
sleep 0.5

rm -rf build && ./build-linux.sh
adb push ./build /home/topeet/code/build/

echo ""
echo "板端: cd /home/topeet/code/build && ./start_rtsp.sh"
ffplay -fflags nobuffer -flags low_delay -framedrop -sync ext rtsp://192.168.31.63:8554/live
