#!/bin/bash
set -e

ROOT_PWD=$(cd "$(dirname $0)" && pwd)

# 你的交叉编译器
GCC_COMPILER=/opt/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu

# OpenCV aarch64
OpenCV_DIR=${ROOT_PWD}/3rdparty/opencv/opencv-linux-aarch64/share/OpenCV

BUILD_DIR=${ROOT_PWD}/build

if [[ ! -d "${BUILD_DIR}" ]]; then
  mkdir -p ${BUILD_DIR}
fi

cd ${BUILD_DIR}
cmake .. \
    -DCMAKE_C_COMPILER=${GCC_COMPILER}-gcc \
    -DCMAKE_CXX_COMPILER=${GCC_COMPILER}-g++ \
    -DOpenCV_DIR=${OpenCV_DIR} \
    -DCMAKE_INSTALL_PREFIX=${BUILD_DIR}

make -j$(nproc)
make install  # 自动拷 .so 到 build 目录

# 创建资源目录
mkdir -p ${BUILD_DIR}/weights ${BUILD_DIR}/images

# 拷贝模型和测试图
cp -r ${ROOT_PWD}/weights/ ${BUILD_DIR}/
cp -r ${ROOT_PWD}/images/  ${BUILD_DIR}/
# cp -r ${ROOT_PWD}/videos/  ${BUILD_DIR}/

# ── RTSP 推流: 启动/停止脚本 ──
cp ${ROOT_PWD}/start_rtsp.sh ${BUILD_DIR}/
cp ${ROOT_PWD}/stop_rtsp.sh  ${BUILD_DIR}/
chmod +x ${BUILD_DIR}/start_rtsp.sh ${BUILD_DIR}/stop_rtsp.sh
# mediamtx.yml 放项目根目录 tools/, start_rtsp.sh 会自动向上找到
echo "💡 板端首次:"
echo "   mkdir -p ~/code/tools && cd ~/code/tools"
echo "   wget https://github.com/bluenviron/mediamtx/releases/download/v1.11.3/mediamtx_v1.11.3_linux_arm64v8.tar.gz"
echo "   tar xzf mediamtx_v*.tar.gz && cp ../../convert_cpp/tools/mediamtx.yml ."

# 从 CMakeLists.txt 里自动读项目名
PROJECT_NAME=$(grep -oP 'project\(\K[^ )]+' ${ROOT_PWD}/CMakeLists.txt | head -1)

echo ""
echo "✅ ${BUILD_DIR}/${PROJECT_NAME}"
echo "   ${BUILD_DIR}/tools/"
echo "   ${BUILD_DIR}/images/"
echo "   ${BUILD_DIR}/weights/"
echo "   ${BUILD_DIR}/start_rtsp.sh"
echo "   ${BUILD_DIR}/stop_rtsp.sh"
echo ""
echo "   推送整包: scp -r build root@<rk3588-ip>:~/yolo_deploy/"
