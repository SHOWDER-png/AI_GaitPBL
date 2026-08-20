# AI_GaitPBL
[The AI Assist gait (walking) training system is a project designed to develop industry-ready Physical AI engineers by building a full-stack, AI-powered hardware system for lower-limb rehabilitation. 

[3.2 - AI Assist Gait (walking) training system.docx](https://github.com/user-attachments/files/28914640/3.2.-.AI.Assist.Gait.walking.training.system.docx)

================================================================================================
Model Weights
================================================================================================
Download the pre-trained YOLOv8 pose model before running the application:

* **YOLOv8 Pose (ONNX/PT): 
cmd : wget -P models/ https://github.com/ultralytics/assets/releases/download/v8.2.0/yolov8n-pose.onnx

Place the downloaded `yolov8n-pose.onnx` file into the `models/` directory.
================================================================================================



================================================================================================
Performance Baseline — macOS M1
================================================================================================
=== Performance Benchmark Report ===
Date:        [16 Aug 2026]
Platform:    macOS M1 (Apple Silicon arm64)
Model:       YOLOv8n-pose ONNX (ONNX Runtime)
Runtime:     ONNX Runtime
Resolution:  1280x720
Frames:      470
Duration:    75.5 sec

Inference time (mean):  77.8 ms   [PASS/FAIL < 100ms]
Inference time (P95):   81.8 ms
FPS (mean):             10.0      [PASS/FAIL > 15 FPS]

=== Pass/Fail ===
Infer < 100ms: PASS (77.8 ms)
FPS > 15:      FAIL (10.0 FPS)