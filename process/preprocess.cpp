#include "core/preprocess.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <cstdio>

cv::Mat preprocess_image(const std::string& image_path) {
    // 1. 读图片
    cv::Mat img = cv::imread(image_path);
    if (img.empty()) {
        fprintf(stderr, "❌ 读图片失败: %s\n", image_path.c_str());
        return img;  // 返回空 Mat, 调用方检查
    }

    // 2. resize → 640×640
    cv::resize(img, img, cv::Size(640, 640));

    // 3. BGR → RGB
    cv::cvtColor(img, img, cv::COLOR_BGR2RGB);

   

    return img;
}
