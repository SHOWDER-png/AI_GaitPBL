# Thingy:53 gait sensor node — integration firmware.
# Each sensor/radio module is compiled only when its Kconfig switch is on (see Kconfig).

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gait_node)

target_include_directories(app PRIVATE src src/plantar)

target_sources(app PRIVATE src/main.c)

target_sources_ifdef(CONFIG_GAIT_IMU app PRIVATE src/imu/imu.c)
target_sources_ifdef(CONFIG_GAIT_PLANTAR app PRIVATE src/plantar/fsr402.c)
target_sources_ifdef(CONFIG_GAIT_BLE app PRIVATE src/ble/gait_ble.c)