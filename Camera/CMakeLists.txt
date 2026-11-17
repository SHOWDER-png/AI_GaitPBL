cmake_minimum_required(VERSION 3.22)
project(posture_detection LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

# ── Detect platform ───────────────────────────────────────────
if(APPLE)
    message(STATUS "Platform: macOS (development)")
    # Homebrew paths for Apple Silicon
    set(HOMEBREW_PREFIX "/opt/homebrew")
    set(ONNXRUNTIME_ROOT "${HOMEBREW_PREFIX}")
    find_package(OpenCV REQUIRED HINTS "${HOMEBREW_PREFIX}/lib/cmake")
else()
    message(STATUS "Platform: Linux aarch64 (FRDM-IMX95)")
    find_package(OpenCV REQUIRED)
    if(NOT DEFINED ONNXRUNTIME_ROOT)
        set(ONNXRUNTIME_ROOT "/root/onnxruntime-linux-aarch64-1.19.0")
    endif()
endif()

# ── Sources ───────────────────────────────────────────────────
add_executable(posture_detection
    src/main.cpp
    src/pose_detector.cpp
    src/angle_calc.cpp
    src/offline_inference.cpp
)

add_executable(camera_capture src/camera_capture.cpp)
target_include_directories(camera_capture PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(camera_capture PRIVATE ${OpenCV_LIBS})

# ── Include dirs ──────────────────────────────────────────────
target_include_directories(posture_detection PRIVATE
    include
    ${OpenCV_INCLUDE_DIRS}
    ${ONNXRUNTIME_ROOT}/include/onnxruntime
    ${ONNXRUNTIME_ROOT}/include
)

# ── Link ──────────────────────────────────────────────────────
target_link_libraries(posture_detection PRIVATE
    ${OpenCV_LIBS}
)

if(APPLE)
    target_link_libraries(posture_detection PRIVATE
        ${ONNXRUNTIME_ROOT}/lib/libonnxruntime.dylib
    )
else()
    target_link_libraries(posture_detection PRIVATE
        ${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so
    )
endif()

# ── macOS frameworks ──────────────────────────────────────────
if(APPLE)
    target_link_libraries(posture_detection PRIVATE
        "-framework Foundation"
        "-framework Accelerate"
    )
endif()

# ── Compile flags ─────────────────────────────────────────────
target_compile_options(posture_detection PRIVATE -O2)

include(CTest)
if(BUILD_TESTING)
    add_executable(offline_validation_tests
        tests/offline_validation_tests.cpp
        src/offline_inference.cpp
    )
    target_include_directories(offline_validation_tests PRIVATE
        src
        ${OpenCV_INCLUDE_DIRS}
        ${ONNXRUNTIME_ROOT}/include/onnxruntime
        ${ONNXRUNTIME_ROOT}/include
    )
    target_link_libraries(offline_validation_tests PRIVATE ${OpenCV_LIBS})
    add_test(NAME offline_validation_tests COMMAND offline_validation_tests)

    add_executable(frame_quality_tests tests/frame_quality_tests.cpp)
    target_include_directories(frame_quality_tests PRIVATE
        src
        ${OpenCV_INCLUDE_DIRS}
        ${ONNXRUNTIME_ROOT}/include/onnxruntime
        ${ONNXRUNTIME_ROOT}/include
    )
    add_test(NAME frame_quality_tests COMMAND frame_quality_tests)

    # Replay over the real fixtures (needs models/yolov8n-pose.onnx); run from Camera/.
    add_executable(frame_quality_replay
        tests/frame_quality_replay.cpp
        src/pose_detector.cpp
    )
    target_include_directories(frame_quality_replay PRIVATE
        src
        ${OpenCV_INCLUDE_DIRS}
        ${ONNXRUNTIME_ROOT}/include/onnxruntime
        ${ONNXRUNTIME_ROOT}/include
    )
    target_link_libraries(frame_quality_replay PRIVATE ${OpenCV_LIBS})
    if(APPLE)
        target_link_libraries(frame_quality_replay PRIVATE ${ONNXRUNTIME_ROOT}/lib/libonnxruntime.dylib)
    else()
        target_link_libraries(frame_quality_replay PRIVATE ${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so)
    endif()
    add_test(NAME frame_quality_replay
        COMMAND frame_quality_replay
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # SCRUM-201: links ONNX Runtime only because pose_detector.h pulls in its C++ API header.
    add_executable(occlusion_fallback_tests
        tests/occlusion_fallback_tests.cpp
        src/angle_calc.cpp
    )
    target_include_directories(occlusion_fallback_tests PRIVATE
        src
        ${OpenCV_INCLUDE_DIRS}
        ${ONNXRUNTIME_ROOT}/include/onnxruntime
        ${ONNXRUNTIME_ROOT}/include
    )
    if(APPLE)
        target_link_libraries(occlusion_fallback_tests PRIVATE ${ONNXRUNTIME_ROOT}/lib/libonnxruntime.dylib)
    else()
        target_link_libraries(occlusion_fallback_tests PRIVATE ${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so)
    endif()
    add_test(NAME occlusion_fallback_tests COMMAND occlusion_fallback_tests)

    # Real-model occlusion replay over the fixtures (needs models/yolov8n-pose.onnx); run from Camera/.
    add_executable(occlusion_replay
        tests/occlusion_replay.cpp
        src/pose_detector.cpp
        src/angle_calc.cpp
    )
    target_include_directories(occlusion_replay PRIVATE
        src
        ${OpenCV_INCLUDE_DIRS}
        ${ONNXRUNTIME_ROOT}/include/onnxruntime
        ${ONNXRUNTIME_ROOT}/include
    )
    target_link_libraries(occlusion_replay PRIVATE ${OpenCV_LIBS})
    if(APPLE)
        target_link_libraries(occlusion_replay PRIVATE ${ONNXRUNTIME_ROOT}/lib/libonnxruntime.dylib)
    else()
        target_link_libraries(occlusion_replay PRIVATE ${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so)
    endif()
    add_test(NAME occlusion_replay
        COMMAND occlusion_replay
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(board_camera_tests tests/board_camera_tests.cpp)
    target_include_directories(board_camera_tests PRIVATE src)
    add_test(NAME board_camera_tests COMMAND board_camera_tests)
endif()