cmake_minimum_required(VERSION 3.20)

# 工具链自动挂载: 裸 `cmake -B build` 即可, 无需显式 -DCMAKE_TOOLCHAIN_FILE
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain.cmake" CACHE FILEPATH "")
endif()

project(uniconnect C ASM)

# 构建类型: Debug=-O0 -g(默认, 与全部历史性能验证位一致) / Release=-Os
# [约定] 切换 Release 属独立流程: 需全回归(CDC/CAN/DAP/fw_update)+stack_watermark 栈水位
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug|Release" FORCE)
endif()
set(CMAKE_C_FLAGS_DEBUG "-O0 -g")
set(CMAKE_C_FLAGS_RELEASE "-Os")

# 全局架构/ABI/通用选项(两固件一致, 与旧 make 体系逐项对齐; -std 与警告级别按目标差异保留)
add_compile_options(
    -march=rv32imacxw
    -mabi=ilp32
    -msmall-data-limit=8
    -msave-restore
    -ffunction-sections
    -fdata-sections
    -fno-common
    -fmessage-length=0
    -fsigned-char
    -fmax-errors=20
    -Wunused
    -Wuninitialized
)

# 产物直接落 build/(fw_update.py 消费 build/uniconnect.bin, stack_watermark.py 消费
# build/uniconnect.map, 路径约定不得变动)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# 模块化目标一律为 INTERFACE 源集(源码随消费者目标编译): 固件镜像要求全套 flags
# (优化等级/BAREMETAL 宏)按目标生效, 这是与 make 语义等价的做法, 也天然解决
# usb_dc_usbhs.c 在 app/boot 两目标下的双宏双编译
add_subdirectory(common)
add_subdirectory(third_party/CherryUSB)
add_subdirectory(third_party/RtthreadNanoPatch)
add_subdirectory(app)
add_subdirectory(boot)

include(cmake/flash.cmake)

# 宿主SDK动态库(嵌套构建, ALL=随 cmake --build build 一起出): 见 sdk/host.cmake
include(sdk/host.cmake)
