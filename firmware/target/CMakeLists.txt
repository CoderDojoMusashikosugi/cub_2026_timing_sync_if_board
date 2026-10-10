# STM32G0B1RCT6 firmware
#
#   cmake -S firmware/target -B firmware/build/target -G Ninja
#   cmake --build firmware/build/target
cmake_minimum_required(VERSION 3.20)

if(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi.cmake)
endif()

project(timing_sync_fw C ASM)

set(FW_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(TP ${FW_ROOT}/third_party)

execute_process(
  COMMAND git describe --always --dirty --abbrev=10
  WORKING_DIRECTORY ${FW_ROOT}
  OUTPUT_VARIABLE FW_GIT_DESCRIBE
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT FW_GIT_DESCRIBE)
  set(FW_GIT_DESCRIBE unknown)
endif()

set(MCU_FLAGS -mcpu=cortex-m0plus -mthumb)
set(COMMON_FLAGS ${MCU_FLAGS} -Os -g3 -ffunction-sections -fdata-sections)
set(WARN_FLAGS -Wall -Wextra -Wshadow -Wstrict-prototypes -Werror)

# ---- CMSIS ----
add_library(cmsis INTERFACE)
target_include_directories(cmsis INTERFACE ${TP}/cmsis_device_g0/Include ${TP}/cmsis_core/Include)
target_compile_definitions(cmsis INTERFACE STM32G0B1xx)

# ---- TinyUSB（vendored subset） ----
add_library(tinyusb STATIC
  ${TP}/tinyusb/src/tusb.c
  ${TP}/tinyusb/src/common/tusb_fifo.c
  ${TP}/tinyusb/src/device/usbd.c
  ${TP}/tinyusb/src/class/cdc/cdc_device.c
  ${TP}/tinyusb/src/portable/st/stm32_fsdev/dcd_stm32_fsdev.c
  ${TP}/tinyusb/src/portable/st/stm32_fsdev/fsdev_common.c)
target_include_directories(tinyusb PUBLIC ${TP}/tinyusb/src ${CMAKE_CURRENT_LIST_DIR}/src)
target_link_libraries(tinyusb PUBLIC cmsis)
target_compile_options(tinyusb PRIVATE ${COMMON_FLAGS})

# ---- core（host単体試験と同じsource） ----
file(GLOB CORE_SRC ${FW_ROOT}/core/src/*.c)
add_library(ts_core STATIC ${CORE_SRC})
target_include_directories(ts_core PUBLIC ${FW_ROOT}/core/include)
target_compile_options(ts_core PRIVATE ${COMMON_FLAGS} ${WARN_FLAGS} -Wpedantic -std=c11)

# ---- firmware image ----
set(IMAGE_SOURCES
  src/startup.c
  src/board.c
  src/console.c
  src/diag.c
  src/main.c
  src/usb_descriptors.c)

# name: image名、nano: newlib-nanoを使うか（%lldが要るselftestは使わない）
function(add_fw_image name nano)
  add_executable(${name} ${IMAGE_SOURCES})
  target_link_libraries(${name} PRIVATE tinyusb ts_core cmsis)
  target_compile_options(${name} PRIVATE ${COMMON_FLAGS} ${WARN_FLAGS} -std=gnu11)
  target_compile_definitions(${name} PRIVATE FW_BUILD_ID="${FW_GIT_DESCRIBE}" FW_BUILD_TYPE="${name}")
  if(nano)
    set(SPECS --specs=nano.specs --specs=nosys.specs)
  else()
    set(SPECS --specs=nosys.specs)
  endif()
  target_link_options(${name} PRIVATE
    ${MCU_FLAGS}
    -T${CMAKE_CURRENT_LIST_DIR}/stm32g0b1xc.ld
    -nostartfiles
    ${SPECS}
    -Wl,--gc-sections
    -Wl,-Map=$<TARGET_FILE_DIR:${name}>/${name}.map
    -Wl,--print-memory-usage)
  set_target_properties(${name} PROPERTIES SUFFIX .elf)
  add_custom_command(TARGET ${name} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${name}> $<TARGET_FILE_DIR:${name}>/${name}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}> $<TARGET_FILE_DIR:${name}>/${name}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${name}>)
endfunction()

# bring-up firmware（全出力High-Zのまま診断する）
add_fw_image(bringup TRUE)

# 製品firmware: GNSSで規律したPPS/RMCをJ3～J8へ出す。JetsonとはUSBの2つ目のCDC
add_fw_image(app TRUE)
target_sources(app PRIVATE src/timesync.c src/jetson_usb.c src/rtc_keep.c)
target_compile_definitions(app PRIVATE FW_APP=1)

# selftest firmware: bring-upに試験用command（ut、fault、loop）を加えたtest build（test_plan §3.3）
# hostのL1試験sourceをそのままMCU上で実行する（UT-13）
add_fw_image(selftest FALSE)
target_sources(selftest PRIVATE src/selftest.c src/selftest_loop.c)
target_include_directories(selftest PRIVATE ${FW_ROOT}/tests/host)
target_compile_definitions(selftest PRIVATE FW_SELFTEST=1)
foreach(t crc32c dpll jetson_proto nmea time)
  set(src ${FW_ROOT}/tests/host/test_${t}.c)
  target_sources(selftest PRIVATE ${src})
  set_source_files_properties(${src} PROPERTIES COMPILE_DEFINITIONS main=ut_${t}_main)
endforeach()
