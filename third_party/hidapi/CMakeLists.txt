# third_party/hidapi -- vendored hidapi 0.15.0 静态库构建胶水
# (上游windows/hid.c为单翻译单元: 内部#include descriptor_reconstruct.c;
#  本文件替换了上游根CMakeLists.txt, 原文件见上游仓库)
# LICENSE: BSD-3(见LICENSE-bsd.txt), 静态链入SDK, 交付面零新增DLL。
# 消费: 仅 sdk/ 经 target_link_libraries(... uni_hidapi); 接口头 hidapi/hidapi.h。
# 平台矩阵: Windows=winapi(windows/hid.c) / macOS=IOHID(mac/hid.c) /
#           Linux=libusb后端(libusb/hid.c, 依赖本仓vendored uni_libusb)。
cmake_minimum_required(VERSION 3.20)

# 头路径独立INTERFACE目标: 两SDK DLL共用(uni_transport.c统一include)
add_library(uni_hidapi_inc INTERFACE)
target_include_directories(uni_hidapi_inc INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}            # <hidapi/hidapi.h> (SDK侧引法)
    ${CMAKE_CURRENT_SOURCE_DIR}/hidapi     # "hidapi.h" (上游各后端直引法)
)

if(WIN32)
    add_library(uni_hidapi STATIC windows/hid.c)
    target_link_libraries(uni_hidapi PUBLIC uni_hidapi_inc)
    target_compile_definitions(uni_hidapi PRIVATE HID_API_NO_EXPORT_DEFINE)
    target_link_libraries(uni_hidapi INTERFACE setupapi hid)
elseif(APPLE)
    add_library(uni_hidapi STATIC mac/hid.c)
    target_link_libraries(uni_hidapi PUBLIC uni_hidapi_inc)
    target_compile_definitions(uni_hidapi PRIVATE HID_API_NO_EXPORT_DEFINE)
    target_include_directories(uni_hidapi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mac)
    target_link_libraries(uni_hidapi INTERFACE
        "-framework CoreFoundation" "-framework IOKit")
else()
    add_library(uni_hidapi STATIC libusb/hid.c)
    target_link_libraries(uni_hidapi PUBLIC uni_hidapi_inc)
    target_compile_definitions(uni_hidapi PRIVATE HID_API_NO_EXPORT_DEFINE)
    # libusb后端: 头与线程来自本仓 vendored uni_libusb(sdk/CMakeLists 先挂libusb)
    target_link_libraries(uni_hidapi PRIVATE uni_libusb)
endif()

target_compile_options(uni_hidapi PRIVATE -w)
# 静态链入共享库(.so/.dylib)必须PIC, 与uni_libusb同策
set_target_properties(uni_hidapi PROPERTIES POSITION_INDEPENDENT_CODE ON)
