# host.cmake -- SDK宿主库嵌套构建(挂进根构建: cmake --build build 一条命令出全套)
#
# 单个CMake configure无法同时编RISC-V固件与宿主DLL -> 对 sdk/ 独立configure到
# build/sdk-host/(自己的编译器与生成器), 嵌套build自管增量, 产物归集 build/sdk/。
#
# 开源工具链纪律(用户决策2026-10-03): 宿主编译器全用GCC, 不用MSVC。
# 发现顺序: 环境变量 GCC_BIN > C:/Portable/mingw64/bin/gcc.exe > PATH里的gcc

if(DEFINED ENV{GCC_BIN} AND EXISTS "$ENV{GCC_BIN}")
    set(UNI_HOST_GCC "$ENV{GCC_BIN}")
elseif(EXISTS "C:/Portable/mingw64/bin/gcc.exe")
    set(UNI_HOST_GCC "C:/Portable/mingw64/bin/gcc.exe")
else()
    set(UNI_HOST_GCC "gcc")
endif()

find_program(UNI_NINJA ninja)

set(UNI_SDK_BUILD_DIR ${CMAKE_BINARY_DIR}/sdk-host)
set(UNI_SDK_DIST_DIR ${CMAKE_BINARY_DIR}/sdk)

if(UNI_NINJA)
    set(_gen -G Ninja)
else()
    set(_gen)   # 让CMake挑默认生成器
endif()

add_custom_target(sdk ALL
    COMMAND ${CMAKE_COMMAND} -S ${CMAKE_SOURCE_DIR}/sdk -B ${UNI_SDK_BUILD_DIR}
            ${_gen} -DCMAKE_C_COMPILER=${UNI_HOST_GCC}
            -DCMAKE_BUILD_TYPE=Release -DSDK_DIST_DIR=${UNI_SDK_DIST_DIR}
    COMMAND ${CMAKE_COMMAND} --build ${UNI_SDK_BUILD_DIR}
    BYPRODUCTS ${UNI_SDK_DIST_DIR}/uniconnect_upgrade.dll ${UNI_SDK_DIST_DIR}/uniconnect_can.dll
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "== [sdk] host DLLs (GCC: ${UNI_HOST_GCC}) =="
    VERBATIM USES_TERMINAL)
