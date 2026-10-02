# BOOT 固件: 裸机(无 RTOS) + CherryUSB 单 HID, flash@0x0 (20KB 分区)
# [Debug 优化等级特例] boot 随 BUILD_TYPE 走, 但 Debug 映射 -O1 而非 -O0:
#   -O0 实测 32512B(158%, 溢出12KB), 且链接面已是最小集(cherryusb_core+hid,
#   cdc/dfu/msc/osal 一概不链), 无可再裁 -- -O1+g 保住可调试性且入分区。
#   Release 恒 -Os(基线口径 15548B/81%)
set(CMAKE_C_FLAGS_DEBUG "-O1 -g")
add_executable(boot.elf
    User/main.c
    User/boot_it.c
    User/bootproto.c
    User/flash_io.c
    User/usb_glue.c
    User/system_ch32v30x.c
    # usb_dc_usbhs.c 直挂而非链 cherryusb_ch32hs: 同文件需按目标双宏双编译
    ${CMAKE_SOURCE_DIR}/third_party/CherryUSB/port/ch32/ch32hs/usb_dc_usbhs.c
    ${CMAKE_SOURCE_DIR}/common/Startup/startup_ch32v30x_D8.S
)

target_include_directories(boot.elf PRIVATE
    User
    ${CMAKE_SOURCE_DIR}/third_party/CherryUSB/port/ch32/ch32hs
)

target_compile_definitions(boot.elf PRIVATE
    CONFIG_USB_BAREMETAL
    CONFIG_USB_DBG_LEVEL=USB_DBG_DISABLE
)

target_compile_options(boot.elf PRIVATE -Wall)

target_link_libraries(boot.elf PRIVATE
    ch32_hal_min
    boot_shared
    cherryusb_core
    cherryusb_hid
)

target_link_options(boot.elf PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/boot.ld
    -nostartfiles
    -Wl,--gc-sections
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,-Map,${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/boot.map
    -Wl,--print-memory-usage
)

add_custom_command(TARGET boot.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:boot.elf>
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/boot.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:boot.elf>
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/boot.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:boot.elf>
    COMMENT "== Generating boot.bin/.hex =="
)
