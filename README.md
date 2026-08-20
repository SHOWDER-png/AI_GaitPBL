# AI_GaitPBL
[The AI Assist gait (walking) training system is a project designed to develop industry-ready Physical AI engineers by building a full-stack, AI-powered hardware system for lower-limb rehabilitation. 

[3.2 - AI Assist Gait (walking) training system.docx](https://github.com/user-attachments/files/28914640/3.2.-.AI.Assist.Gait.walking.training.system.docx)

Model Weights
Download the pre-trained YOLOv8 pose model before running the application:

* **YOLOv8 Pose (ONNX/PT): 
cmd : wget -P models/ https://github.com/ultralytics/assets/releases/download/v8.2.0/yolov8n-pose.onnx

Place the downloaded `yolov8n-pose.onnx` file into the `models/` directory.



Performance Baseline — macOS M1
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

[Software requirement](https://kmitlthailand-my.sharepoint.com/:w:/g/personal/66991004_kmitl_ac_th/IQAkcqkX_gSVQJa_L1V5SK93AVTThtbH7hlDj0av42PS8dA?e=Zjrq94)

[Software Detailed](https://kmitlthailand-my.sharepoint.com/:w:/g/personal/66991005_kmitl_ac_th/IQCsIy6CBysZSKwTphktBB20AdmFBRBxNV63HQNozG7AbfU?e=dE5D8I&isSPOFile=1&OR=TEAMS-MAGLEV.null_ns.rwc&wdExp=TEAMS-TREATMENT&CT=1785736045185&web=1&TeamsCID=70b1213c-965a-4016-a4e4-fb5e58eafcda&clickparams=eyJBcHBOYW1lIjoiVGVhbXMtRGVza3RvcCIsIkFwcFZlcnNpb24iOiI0OS8yNjA3MTYxNjAwOCJ9&linkOpenTime=1785736048702&xsdata=MDV8MDJ8fGUyM2FkNGU4ZmJmMzRhMDg5NTk0MDhkZWZkZDVkZjI4fGZkMjA2NzE1NzUwOTRhZTU5Yjk2NzZiYjk3ODg2YTg0fDB8MHw2MzkyMjcyOTIxNDcwMzY4NjF8VW5rbm93bnxWR1ZoYlhOVFpXTjFjbWwwZVZObGNuWnBZMlY4ZXlKRFFTSTZJbFJsWVcxelgwRlVVRk5sY25acFkyVmZVMUJQVEU5R0lpd2lWaUk2SWpBdU1DNHdNREF3SWl3aVVDSTZJbGRwYmpNeUlpd2lRVTRpT2lKUGRHaGxjaUlzSWxkVUlqb3hNWDA9fDF8TDJOb1lYUnpMekU1T2pabVltUXlNemd3TWpnMU56UXlPV0U0TnpjNE5EUmlNemhsTXpjMFl6RTJRSFJvY21WaFpDNTJNaTl0WlhOellXZGxjeTh4TnpnM01UTXlOREV4TkRjMnxjMGRiODkwNGFmM2E0OGJkMmFkMzA4ZGVmZGQ1ZGYyOHxmZWM2MzE1YjNjMmI0ODM3YTFkZWVmOGNhZmFlMTQwMA%3D%3D&sdata=c1d1RnFSMWFrS3J6Ti9rdFNBdnZEZVgzVDVwSWl5VjRYVEpDOWR5VFVCVT0%3D&ovuser=fd206715-7509-4ae5-9b96-76bb97886a84%2C66991004%40KMITL.AC.TH)

[Software Architecture](https://kmitlthailand-my.sharepoint.com/:w:/g/personal/66991005_kmitl_ac_th/IQBDY02e8OOYToZluZhL_g95AV6QLbRNw_6himdHaDuPs8o?e=rk4iz3)
