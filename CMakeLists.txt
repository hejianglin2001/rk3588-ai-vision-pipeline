cmake_minimum_required(VERSION 3.10)
project(yolo_inference LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--allow-shlib-undefined")

# ── RPATH: 运行时在可执行文件同目录下找 .so ──
set(CMAKE_SKIP_INSTALL_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

# 源文件
file(GLOB_RECURSE SOURCES
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/process/*.cpp
)
add_executable(${PROJECT_NAME} ${SOURCES})

# 头文件
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/librknn_api/include
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/process
    ${CMAKE_SOURCE_DIR}/3rdparty/mosquitto
    ${CMAKE_SOURCE_DIR}/3rdparty/opencv/opencv-linux-aarch64/include/opencv4
    ${CMAKE_SOURCE_DIR}/3rdparty/rga/include
    ${CMAKE_SOURCE_DIR}/3rdparty/mpp/include
)

# OpenCV 4.7
set(OCV ${CMAKE_SOURCE_DIR}/3rdparty/opencv/opencv-linux-aarch64/lib)
set(OpenCV_LIBS
    ${OCV}/libopencv_core.so     ${OCV}/libopencv_core.so.407     ${OCV}/libopencv_core.so.4.7.0
    ${OCV}/libopencv_imgproc.so  ${OCV}/libopencv_imgproc.so.407  ${OCV}/libopencv_imgproc.so.4.7.0
    ${OCV}/libopencv_imgcodecs.so ${OCV}/libopencv_imgcodecs.so.407 ${OCV}/libopencv_imgcodecs.so.4.7.0
    ${OCV}/libopencv_highgui.so  ${OCV}/libopencv_highgui.so.407  ${OCV}/libopencv_highgui.so.4.7.0
)
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/librknn_api/aarch64/librknnrt.so
    ${CMAKE_SOURCE_DIR}/3rdparty/mosquitto/libmosquitto.so.1
    ${OpenCV_LIBS}
    ${CMAKE_SOURCE_DIR}/3rdparty/rga/libs/Linux/gcc-aarch64/librga.so
    ${CMAKE_SOURCE_DIR}/3rdparty/mpp/librockchip_mpp.so
    pthread dl
)

# ── 打包: install 时把 .so 一起拷到 build ──
install(TARGETS ${PROJECT_NAME} DESTINATION .)
install(PROGRAMS ${OpenCV_LIBS} DESTINATION lib)
install(PROGRAMS ${CMAKE_SOURCE_DIR}/3rdparty/mosquitto/libmosquitto.so.1 DESTINATION lib)
install(PROGRAMS ${CMAKE_SOURCE_DIR}/3rdparty/rga/libs/Linux/gcc-aarch64/librga.so DESTINATION lib)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/3rdparty/rga/libs/Linux/gcc-aarch64/ DESTINATION lib
        FILES_MATCHING PATTERN "*.so*")
