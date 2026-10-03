# third_party/libusb -- vendored libusb v1.0.30 静态库构建胶水
# 上游无CMake(autotools/MSVC), 本文件按 msvc/libusb_static.vcxproj 的源清单直编。
# LICENSE: LGPL-2.1(随仓库vendored满足源码可得性), 静态链入SDK两DLL。
# 消费: 仅 sdk/ 经 target_link_libraries(... uni_libusb); 接口头 libusb/libusb.h。
# 平台配置: uni_config/config.h 单文件三平台(_WIN32/__APPLE__/POSIX)。
cmake_minimum_required(VERSION 3.20)

add_library(uni_libusb STATIC
    libusb/core.c
    libusb/descriptor.c
    libusb/hotplug.c
    libusb/io.c
    libusb/strerror.c
    libusb/sync.c
)

if(WIN32)
    # 注: 不编译 windows_hotplug.c(热插拔通知线程)——SDK只用按需scan, 无热插拔需求;
    # 且其后台线程存在进程退出期加载器锁死锁隐患(2026-10-03 test_abi间歇挂死实锚),
    # 上游全部引用经 LIBUSB_WINDOWS_HOTPLUG 宏门控, 摘除后走无热插拔回退路径。
    target_sources(uni_libusb PRIVATE
        libusb/os/events_windows.c
        libusb/os/threads_windows.c
        libusb/os/windows_common.c
        libusb/os/windows_usbdk.c
        libusb/os/windows_winusb.c
    )
    # windows后端: WinUsb(收发) + SetupAPI(枚举)
    target_link_libraries(uni_libusb INTERFACE winusb setupapi)
elseif(APPLE)
    target_sources(uni_libusb PRIVATE
        libusb/os/events_posix.c
        libusb/os/threads_posix.c
        libusb/os/darwin_usb.c
    )
    target_link_libraries(uni_libusb INTERFACE
        "-framework CoreFoundation" "-framework IOKit" "-framework Security")
else()
    target_sources(uni_libusb PRIVATE
        libusb/os/events_posix.c
        libusb/os/threads_posix.c
        libusb/os/linux_usbfs.c
        libusb/os/linux_netlink.c   # 热插拔(内核netlink, 不依赖libudev)
    )
    find_package(Threads REQUIRED)
    target_link_libraries(uni_libusb INTERFACE Threads::Threads)
endif()

target_include_directories(uni_libusb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/uni_config   # <config.h> (libusbi.h 无条件包含)
    ${CMAKE_CURRENT_SOURCE_DIR}/libusb       # <libusb.h>
)
# vendored上游源码的告警不属于本仓治理范围
target_compile_options(uni_libusb PRIVATE -w)
