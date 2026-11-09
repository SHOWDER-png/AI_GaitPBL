#pragma once
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

struct Landmark {
    float x, y;      // pixel coordinates
    float score;     // confidence
};

struct Detection {
    cv::Rect2f      bbox;        // bounding box (normalized)
    float           conf;        // person confidence
    std::vector<Landmark> kpts;  // 17 keypoints
};

class PoseDetector {
public:
    explicit PoseDetector(const std::string& model_path);
    std::vector<Detection> detect(const cv::Mat& frame, float conf_thresh = 0.3f);

    // YOLOv8-pose keypoint indices (COCO format)
    static constexpr int NOSE        = 0;
    static constexpr int L_SHOULDER  = 5;
    static constexpr int R_SHOULDER  = 6;
    static constexpr int L_HIP       = 11;
    static constexpr int R_HIP       = 12;
    static constexpr int L_KNEE      = 13;
    static constexpr int R_KNEE      = 14;
    static constexpr int L_ANKLE     = 15;
    static constexpr int R_ANKLE     = 16;

private:
    Ort::Env            env_;
    Ort::Session        session_{nullptr};
    Ort::SessionOptions opts_;
    int input_w_ = 640, input_h_ = 640;

    cv::Mat preprocess(const cv::Mat& frame, float& scale, int& pad_x, int& pad_y);
    std::vector<Detection> postprocess(
        const float* data, int rows,
        float scale, int pad_x, int pad_y,
        int orig_w, int orig_h, float conf_thresh);
};