#pragma once
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

// 预处理一张图片 → uint8[640][640][3], HWC, BGR→RGB
cv::Mat preprocess_image(const std::string& image_path);
