# third_party/hidapi -- vendored hidapi 0.15.0 静态库构建胶水
# (上游windows/hid.c为单翻译单元: 内部#include descriptor_reconstruct.c;
#  本文件替换了上游根CMakeLists.txt, 原文件见上游仓库)
# LICENSE: BSD-3(见LICENSE-bsd.txt), 静态链入SDK, 交付面零新增DLL。
# 消费: 仅 sdk/ 经 target_link_libraries(... uni_hidapi); 接口头 hidapi/hidapi.h。
cmake_minimum_required(VERSION 3.20)

if(WIN32)
    # 头路径独立INTERFACE目标: 两SDK DLL共用(uni_transport.c统一include);
    # 静态库本体仅boot HID消费方(uniconnect_upgrade)链接
    add_library(uni_hidapi_inc INTERFACE)
    target_include_directories(uni_hidapi_inc INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}            # <hidapi/hidapi.h> (SDK侧引法)
        ${CMAKE_CURRENT_SOURCE_DIR}/hidapi     # "hidapi.h" (上游windows/hid.c直引法)
        ${CMAKE_CURRENT_SOURCE_DIR}/windows    # 兼容头(hidapi_hidsdi.h等, 优先于MinGW自带)
    )
    add_library(uni_hidapi STATIC
        windows/hid.c
    )
    target_link_libraries(uni_hidapi PUBLIC uni_hidapi_inc)
    # WinAPI: SetupAPI(枚举) + hid.dll(HidD_*)
    target_link_libraries(uni_hidapi INTERFACE setupapi hid)
    target_compile_definitions(uni_hidapi PRIVATE HID_API_NO_EXPORT_DEFINE)
else()
    message(FATAL_ERROR "hidapi胶水当前只接线了Windows后端; Linux(libusb后端)按README规划在Linux目标开放时补充")
endif()

target_compile_options(uni_hidapi PRIVATE -w)
