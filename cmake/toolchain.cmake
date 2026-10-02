# riscv-none-embed 工具链(MRS 自带 GCC 8)
# 路径可用环境变量 TOOLCHAIN_BIN 覆盖(正斜杠形式), 默认取 MRS 安装位置
if(DEFINED ENV{TOOLCHAIN_BIN})
    set(TOOLCHAIN_BIN "$ENV{TOOLCHAIN_BIN}")
else()
    set(TOOLCHAIN_BIN "C:/MounRiver/MounRiver_Studio2/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC/bin")
endif()

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)
# 交叉目标无法执行产物, configure 期探针改静态库
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TOOLCHAIN_PREFIX riscv-none-embed-)
find_program(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}gcc      HINTS ${TOOLCHAIN_BIN} REQUIRED)
find_program(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc      HINTS ${TOOLCHAIN_BIN} REQUIRED)
find_program(CMAKE_AR           ${TOOLCHAIN_PREFIX}ar       HINTS ${TOOLCHAIN_BIN} REQUIRED)
find_program(CMAKE_OBJCOPY      ${TOOLCHAIN_PREFIX}objcopy  HINTS ${TOOLCHAIN_BIN} REQUIRED)
find_program(CMAKE_OBJDUMP      ${TOOLCHAIN_PREFIX}objdump  HINTS ${TOOLCHAIN_BIN} REQUIRED)
find_program(CMAKE_SIZE         ${TOOLCHAIN_PREFIX}size     HINTS ${TOOLCHAIN_BIN} REQUIRED)
