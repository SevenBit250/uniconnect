# 公共层: 厂商库(WCH) + 启动文件 + boot/APP 共享头
# 归属实证(源码归属矩阵, 2026-10-03): Core/Peripheral/Startup 为 app+boot 共享,
# 但编译面不同 -- APP 全量 26 个外设 .c, boot 最小集 4 个(rcc/gpio/flash/misc)

add_library(ch32_hal_full INTERFACE)
target_sources(ch32_hal_full INTERFACE
    Core/core_riscv.c
    Peripheral/src/ch32v30x_adc.c
    Peripheral/src/ch32v30x_bkp.c
    Peripheral/src/ch32v30x_can.c
    Peripheral/src/ch32v30x_crc.c
    Peripheral/src/ch32v30x_dac.c
    Peripheral/src/ch32v30x_dbgmcu.c
    Peripheral/src/ch32v30x_dma.c
    Peripheral/src/ch32v30x_dvp.c
    Peripheral/src/ch32v30x_eth.c
    Peripheral/src/ch32v30x_exti.c
    Peripheral/src/ch32v30x_flash.c
    Peripheral/src/ch32v30x_fsmc.c
    Peripheral/src/ch32v30x_gpio.c
    Peripheral/src/ch32v30x_i2c.c
    Peripheral/src/ch32v30x_iwdg.c
    Peripheral/src/ch32v30x_misc.c
    Peripheral/src/ch32v30x_opa.c
    Peripheral/src/ch32v30x_pwr.c
    Peripheral/src/ch32v30x_rcc.c
    Peripheral/src/ch32v30x_rng.c
    Peripheral/src/ch32v30x_rtc.c
    Peripheral/src/ch32v30x_sdio.c
    Peripheral/src/ch32v30x_spi.c
    Peripheral/src/ch32v30x_tim.c
    Peripheral/src/ch32v30x_usart.c
    Peripheral/src/ch32v30x_wwdg.c
)
target_include_directories(ch32_hal_full INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/Peripheral/inc
)

# boot 专用最小集(20KB 分区约束; boot.ld 有 ASSERT 兜底)
add_library(ch32_hal_min INTERFACE)
target_sources(ch32_hal_min INTERFACE
    Core/core_riscv.c
    Peripheral/src/ch32v30x_rcc.c
    Peripheral/src/ch32v30x_gpio.c
    Peripheral/src/ch32v30x_flash.c
    Peripheral/src/ch32v30x_misc.c
)
target_include_directories(ch32_hal_min INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/Peripheral/inc
)

# 共享头: boot_shared.h(APP/boot 契约) + system_ch32v30x.h(.c 两侧有意分叉: APP 144M / boot 96M)
add_library(boot_shared INTERFACE)
target_include_directories(boot_shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
