#include "pose_detector.h"
#include <iostream>
#include <algorithm>
#include <numeric>

PoseDetector::PoseDetector(const std::string& model_path)
    : env_(ORT_LOGGING_LEVEL_WARNING, "PoseDetector")
{
    opts_.SetIntraOpNumThreads(2);
    opts_.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
    session_ = Ort::Session(env_, model_path.c_str(), opts_);
    std::cout << "[PoseDetector] Loaded: " << model_path << "\n";
}

// Letterbox resize — keep aspect ratio
cv::Mat PoseDetector::preprocess(const cv::Mat& frame,
                                  float& scale, int& pad_x, int& pad_y) {
    float sw = (float)input_w_ / frame.cols;
    float sh = (float)input_h_ / frame.rows;
    scale = std::min(sw, sh);

    int new_w = (int)(frame.cols * scale);
    int new_h = (int)(frame.rows * scale);
    pad_x = (input_w_ - new_w) / 2;
    pad_y = (input_h_ - new_h) / 2;

    cv::Mat resized;
    cv::resize(frame, resized, {new_w, new_h});
    cv::cvtColor(resized, resized, cv::COLOR_BGR2RGB);

    cv::Mat canvas(input_h_, input_w_, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(canvas(cv::Rect(pad_x, pad_y, new_w, new_h)));

    canvas.convertTo(canvas, CV_32FC3, 1.0f / 255.0f);
    return canvas;
}

std::vector<Detection> PoseDetector::postprocess(
    const float* data, int rows,
    float scale, int pad_x, int pad_y,
    int orig_w, int orig_h, float conf_thresh)
{
    // YOLOv8-pose output: [1, 56, 8400]
    // Each col: cx, cy, w, h, conf, kpt0_x, kpt0_y, kpt0_s, ... x17
    std::vector<Detection> detections;

    for (int i = 0; i < rows; ++i) {
        float conf = data[4 * 8400 + i];  // column-major layout
        if (conf < conf_thresh) continue;

        Detection det;
        det.conf = conf;

        // Bounding box (center format → convert to pixel)
        float cx = data[0 * 8400 + i];
        float cy = data[1 * 8400 + i];
        float bw = data[2 * 8400 + i];
        float bh = data[3 * 8400 + i];

        // Unpad and unscale
        det.bbox.x = ((cx - bw/2) - pad_x) / scale;
        det.bbox.y = ((cy - bh/2) - pad_y) / scale;
        det.bbox.width  = bw / scale;
        det.bbox.height = bh / scale;

        // 17 Keypoints (each: x, y, score)
        det.kpts.resize(17);
        for (int k = 0; k < 17; ++k) {
            float kx = data[(5 + k*3 + 0) * 8400 + i];
            float ky = data[(5 + k*3 + 1) * 8400 + i];
            float ks = data[(5 + k*3 + 2) * 8400 + i];
            det.kpts[k].x     = (kx - pad_x) / scale;
            det.kpts[k].y     = (ky - pad_y) / scale;
            det.kpts[k].score = ks;
        }

        detections.push_back(det);
    }

    // Keep highest confidence detection only (1 person)
    if (detections.size() > 1) {
        std::sort(detections.begin(), detections.end(),
            [](auto& a, auto& b){ return a.conf > b.conf; });
        detections.resize(1);
    }
    return detections;
}

std::vector<Detection> PoseDetector::detect(const cv::Mat& frame, float conf_thresh) {
    float scale; int pad_x, pad_y;
    cv::Mat input = preprocess(frame, scale, pad_x, pad_y);

    // Build tensor [1, 3, 640, 640]
    std::vector<float> tensor_data;
    std::vector<cv::Mat> chans(3);
    cv::split(input, chans);
    for (auto& ch : chans)
        tensor_data.insert(tensor_data.end(),
            (float*)ch.data, (float*)ch.data + input_w_ * input_h_);

    std::vector<int64_t> shape = {1, 3, input_h_, input_w_};
    auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value in_tensor = Ort::Value::CreateTensor<float>(
        mem, tensor_data.data(), tensor_data.size(),
        shape.data(), shape.size());

    const char* in_names[]  = {"images"};
    const char* out_names[] = {"output0"};
    auto output = session_.Run(
        Ort::RunOptions{nullptr},
        in_names, &in_tensor, 1,
        out_names, 1);

    // Output shape: [1, 56, 8400]
    auto out_shape = output[0].GetTensorTypeAndShapeInfo().GetShape();
    int rows = (int)out_shape[2];  // 8400
    const float* out_data = output[0].GetTensorData<float>();

    return postprocess(out_data, rows, scale, pad_x, pad_y,
                       frame.cols, frame.rows, conf_thresh);
}