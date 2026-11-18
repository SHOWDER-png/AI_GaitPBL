# Thingy:53 gait sensor node — integration firmware.
# Each sensor/radio module is compiled only when its Kconfig switch is on (see Kconfig).
# Every .c file is listed explicitly: a missing one gives "undefined reference".

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gait_node)

# Lets sources include "system/system_config.h", "imu/imu.h", ... from any subfolder
target_include_directories(app PRIVATE src src/plantar)

target_sources(app PRIVATE src/main.c)

target_sources_ifdef(CONFIG_GAIT_IMU app PRIVATE
	src/imu/imu.c
	src/imu/imu_thread.c)
target_sources_ifdef(CONFIG_GAIT_PLANTAR app PRIVATE
	src/plantar/fsr402.c
	src/plantar/plantar_thread.c)
target_sources_ifdef(CONFIG_GAIT_BLE app PRIVATE src/ble/gait_ble.c)
if(CONFIG_GAIT_BLE AND CONFIG_GAIT_PLANTAR)
	target_sources(app PRIVATE src/ble/ble_thread.c)
endif()
