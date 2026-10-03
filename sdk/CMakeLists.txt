# UniConnect SDK -- 宿主动态库(开源工具链: GCC/MinGW-w64, 拒绝MSVC)
# 由根构建经 sdk/host.cmake 嵌套调用(独立configure于 build/sdk-host/),
# 也可手工: cmake -S sdk -B build-sdk -G Ninja -DCMAKE_C_COMPILER=<gcc> -DSDK_DIST_DIR=<dir>
cmake_minimum_required(VERSION 3.20)
project(uniconnect-sdk C)

set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
if(NOT SDK_DIST_DIR)
    set(SDK_DIST_DIR ${CMAKE_BINARY_DIR}/dist)
endif()
# 导入库(.dll.a)与dll同层, 复制路径确定化
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# 固件协议头单一事实源(host-safe 契约, 构建即检测)
set(FW_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# 传输后端: vendored libusb v1.0.30(WinUSB bulk) + hidapi 0.15.0(boot HID),
# 均静态链入(交付面零新增dll); 许可证LGPL/BSD随仓库vendored满足源码可得性
add_subdirectory(${FW_ROOT}/third_party/libusb ${CMAKE_BINARY_DIR}/libusb-build)
add_subdirectory(${FW_ROOT}/third_party/hidapi  ${CMAKE_BINARY_DIR}/hidapi-build)

# 交付头聚合: 5头(源) -> 3头(交付, 自包含, uniconnect_*): 内联固件同源头
set(UNI_DIST_INCLUDE ${CMAKE_BINARY_DIR}/dist-include)
find_program(UNI_PYTHON NAMES python3 python REQUIRED)   # CI的ubuntu镜像无裸python
execute_process(COMMAND ${CMAKE_COMMAND} -E env
                        ${UNI_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/amalgamate.py
                        ${UNI_DIST_INCLUDE} COMMAND_ERROR_IS_FATAL ANY)

function(uni_add_dll name)
    add_library(${name} SHARED ${ARGN})
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${FW_ROOT}/common          # can_proto.h / usb_ids.h / boot_shared.h
        ${FW_ROOT}/boot/User       # boot.h(HID升级协议)
    )
    target_compile_definitions(${name} PRIVATE UNI_SDK_BUILD)
    # 传输后端静态链入: libusb(WinUSB bulk) + hidapi(boot HID, uni_transport共享源引用)
    target_link_libraries(${name} PRIVATE uni_libusb uni_hidapi)
    set_target_properties(${name} PROPERTIES PREFIX "")   # uniconnect_can.dll 而非 libuniconnect_can.dll
    target_compile_options(${name} PRIVATE -Wall -Wextra)

    # 导入库仅Windows存在(ELF/Mach-O无此概念, IMPORT_LIBRARY_SUFFIX为空)
    set(_implib_copy)
    if(CMAKE_IMPORT_LIBRARY_SUFFIX)
        set(_implib_copy
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_BINARY_DIR}/lib${name}${CMAKE_IMPORT_LIBRARY_SUFFIX} ${SDK_DIST_DIR}/)
    endif()

    # 交付物归集: dll + 导入库(.dll.a) + .def(每目标各自产物, 无共享文件)
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${name}> ${SDK_DIST_DIR}/
        ${_implib_copy}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/src/${name}.def ${SDK_DIST_DIR}/
        COMMENT "== Collecting ${name} -> ${SDK_DIST_DIR} ==" VERBATIM)
endfunction()

uni_add_dll(uniconnect_upgrade src/uniconnect_upgrade.c src/uni_transport.c)
uni_add_dll(uniconnect_can    src/uniconnect_can.c    src/uni_transport.c)

# 交付头归集为单写者目标: 两DLL的POST_BUILD并行拷同一批头会互相踩踏
# (linux CI实锚: 两copy_if_different同毫秒并发, 后者报Error copying)
add_custom_target(sdk_dist_headers
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SDK_DIST_DIR}/include
    # 聚合交付=仅三头: 清掉历史布局残留
    COMMAND ${CMAKE_COMMAND} -E rm -f ${SDK_DIST_DIR}/include/usb_ids.h
            ${SDK_DIST_DIR}/include/can_proto.h
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${UNI_DIST_INCLUDE}/uniconnect_common.h
            ${UNI_DIST_INCLUDE}/uniconnect_upgrade.h
            ${UNI_DIST_INCLUDE}/uniconnect_can.h
            ${SDK_DIST_DIR}/include/
    COMMENT "== Collecting dist headers -> ${SDK_DIST_DIR}/include ==" VERBATIM)
add_dependencies(uniconnect_upgrade sdk_dist_headers)
add_dependencies(uniconnect_can sdk_dist_headers)

# MSVC 兼容导入库(.lib): MinGW binutils dlltool 从 .def 生成 COFF 格式,
# MSVC 链接器可直接消费(工具缺失则跳过, .def 兜底由消费端自行生成)
get_filename_component(_gcc_dir ${CMAKE_C_COMPILER} DIRECTORY)
find_program(UNI_DLLTOOL dlltool HINTS ${_gcc_dir})
if(UNI_DLLTOOL)
    foreach(_lib uniconnect_upgrade uniconnect_can)
        add_custom_command(TARGET ${_lib} POST_BUILD
            COMMAND ${UNI_DLLTOOL} -m i386:x86-64
                    -d ${CMAKE_CURRENT_SOURCE_DIR}/src/${_lib}.def
                    -D $<TARGET_FILE:${_lib}>
                    -l ${SDK_DIST_DIR}/${_lib}.lib
            COMMENT "== dlltool ${_lib}.lib (MSVC import) ==" VERBATIM)
    endforeach()
else()
    message(STATUS "dlltool 未找到: 跳过 .lib 生成(消费端可用 .def 自行生成)")
endif()

enable_testing()
add_subdirectory(tests)
