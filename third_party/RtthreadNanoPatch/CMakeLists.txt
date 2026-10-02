# RT-Thread Nano(vendored, 固定 v3.1.5) + 本仓 Patch 层 -- 仅 APP 使用
#
# Patch 覆盖机制(与旧 make 体系等价, 三条铁律):
# 1. Nano 子模块只编 rt-thread/src 下 15 个内核 .c
# 2. libcpu 用 Patch 副本(cpuport/context_gcc/interrupt_gcc, 去 PLIC 等本芯片无关项)
# 3. rtconfig.h/board.h/completion 等只来自 Patch(-I 命中)

set(NANO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../RtthreadNano/rt-thread/src)

add_library(rtthread INTERFACE)
target_sources(rtthread INTERFACE
    ${NANO_SRC}/clock.c
    ${NANO_SRC}/components.c
    ${NANO_SRC}/cpu.c
    ${NANO_SRC}/idle.c
    ${NANO_SRC}/ipc.c
    ${NANO_SRC}/irq.c
    ${NANO_SRC}/kservice.c
    ${NANO_SRC}/mem.c
    ${NANO_SRC}/memheap.c
    ${NANO_SRC}/mempool.c
    ${NANO_SRC}/object.c
    ${NANO_SRC}/scheduler.c
    ${NANO_SRC}/slab.c
    ${NANO_SRC}/thread.c
    ${NANO_SRC}/timer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/board.c
    ${CMAKE_CURRENT_SOURCE_DIR}/completion.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libcpu/risc-v/common/cpuport.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libcpu/risc-v/common/context_gcc.S
    ${CMAKE_CURRENT_SOURCE_DIR}/libcpu/risc-v/common/interrupt_gcc.S
)
target_include_directories(rtthread INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/libcpu/risc-v
    ${CMAKE_CURRENT_SOURCE_DIR}/libcpu/risc-v/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../RtthreadNano/rt-thread/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../RtthreadNano/rt-thread/include/libc
    ${NANO_SRC}
)
target_compile_definitions(rtthread INTERFACE __RTTHREAD__)
