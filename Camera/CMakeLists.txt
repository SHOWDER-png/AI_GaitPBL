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
    # Set via cmake args when cross-compiling
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
    ${ONNXRUNTIME_ROOT}/lib/libonnxruntime.dylib
)

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
endif()