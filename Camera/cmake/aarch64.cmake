# Cross-compilation toolchain for the FRDM-i.MX95 (Linux, Cortex-A55, aarch64). SCRUM-176.
#
#   cmake -S Camera -B build-imx95 -DCMAKE_TOOLCHAIN_FILE=Camera/cmake/aarch64.cmake \
#         -DCMAKE_BUILD_TYPE=Release
#
# Two ways to supply the compiler and the target libraries (OpenCV with GStreamer, glibc):
#
#  1. NXP Yocto SDK (recommended, matches the board image 6.18-whinlatter exactly):
#       source /opt/fsl-imx-*/environment-setup-armv8a-poky-linux
#     The SDK script exports CC/CXX and OECORE_TARGET_SYSROOT; this file picks them up.
#
#  2. A generic cross compiler plus a sysroot copied from the board:
#       sudo apt install g++-aarch64-linux-gnu           (Ubuntu/Debian host)
#       export FRDM_SYSROOT=/path/to/rootfs-copy          (needs /usr/include/opencv4 + libs)
#
# The board image has no OpenCV C++ headers or opencv4.pc (SCRUM-191 report), so for both ways
# the sysroot must contain OpenCV's development files. Without them configuration stops at
# find_package(OpenCV) with a clear error, which is the intended behaviour.
#
# ONNX Runtime is not in the sysroot: Camera/scripts/fetch_onnxruntime.sh downloads the
# official prebuilt aarch64 package into Camera/third_party/, and CMakeLists.txt finds it there.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# ── Compiler ──────────────────────────────────────────────────
# Respect CC/CXX from the NXP SDK environment; otherwise use the Debian/Ubuntu cross compiler.
if(NOT CMAKE_C_COMPILER)
    if(DEFINED ENV{CC} AND DEFINED ENV{OECORE_TARGET_SYSROOT})
        separate_arguments(_cc UNIX_COMMAND "$ENV{CC}")
        list(GET _cc 0 CMAKE_C_COMPILER)
    else()
        set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
    endif()
endif()
if(NOT CMAKE_CXX_COMPILER)
    if(DEFINED ENV{CXX} AND DEFINED ENV{OECORE_TARGET_SYSROOT})
        separate_arguments(_cxx UNIX_COMMAND "$ENV{CXX}")
        list(GET _cxx 0 CMAKE_CXX_COMPILER)
    else()
        set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
    endif()
endif()

# ── Sysroot ───────────────────────────────────────────────────
if(DEFINED ENV{OECORE_TARGET_SYSROOT})
    set(CMAKE_SYSROOT "$ENV{OECORE_TARGET_SYSROOT}")
elseif(DEFINED ENV{FRDM_SYSROOT})
    set(CMAKE_SYSROOT "$ENV{FRDM_SYSROOT}")
endif()
# Keep the variables visible to try_compile() projects as well.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CMAKE_SYSROOT)

# Cortex-A55 tuning; safe for any ARMv8.2-A core.
set(CMAKE_C_FLAGS_INIT   "-mcpu=cortex-a55")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-a55")

# ── Search rules: programs from the host, libraries/headers from the target ──
if(CMAKE_SYSROOT)
    set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Cross-built test binaries cannot run on the build host; ctest uses qemu when it is installed.
find_program(GAIT_QEMU_AARCH64 qemu-aarch64)
if(GAIT_QEMU_AARCH64)
    if(CMAKE_SYSROOT)
        set(CMAKE_CROSSCOMPILING_EMULATOR "${GAIT_QEMU_AARCH64};-L;${CMAKE_SYSROOT}")
    else()
        set(CMAKE_CROSSCOMPILING_EMULATOR "${GAIT_QEMU_AARCH64};-L;/usr/aarch64-linux-gnu")
    endif()
endif()
