# APP 固件: RT-Thread Nano + 全功能(CDC/DAP/CAN/LED/看门狗), flash@0x5000
add_executable(uniconnect.elf
    Application/can_bridge_thread.c
    Application/cdc_uart_thread.c
    Application/dap_thread.c
    Application/monitor_thread.c
    Bsp/device/usb/bsp_usb.c
    Bsp/device/uart/bsp_uart.c
    Bsp/device/led/bsp_led.c
    Bsp/device/dap/bsp_dap.c
    Bsp/device/can/bsp_can.c
    Bsp/components/watchdog/bsp_watchdog.c
    Bsp/components/block_buffer_pool/bsp_pool.c
    Dap/src/DAP.c
    Dap/src/DAP_UART.c
    Dap/src/DAP_queue.c
    Dap/src/DAP_vendor.c
    Dap/src/JTAG_DP.c
    Dap/src/SWO.c
    Dap/src/SW_DP.c
    User/main.c
    User/ch32v30x_it.c
    User/system_ch32v30x.c
    User/debug.c
    ${CMAKE_SOURCE_DIR}/common/Startup/startup_ch32v30x_D8C.S
)

target_include_directories(uniconnect.elf PRIVATE
    Application
    Bsp
    Bsp/device/can
    Bsp/device/led
    Bsp/device/uart
    Bsp/device/usb
    Bsp/device/dap
    Bsp/components/block_buffer_pool
    Bsp/components/watchdog
    Dap
    Dap/inc
    User
)

# 旧 make 体系为 gnu99(boot 无显式 std, 保持各自口径)
target_compile_options(uniconnect.elf PRIVATE -std=gnu99)

target_link_libraries(uniconnect.elf PRIVATE
    ch32_hal_full
    rtthread
    boot_shared
    cherryusb_core
    cherryusb_cdc
    cherryusb_dfu
    cherryusb_hid
    cherryusb_msc
    cherryusb_osal_rtthread
    cherryusb_ch32hs
    m
)

target_link_options(uniconnect.elf PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/app.ld
    -nostartfiles
    -Wl,--gc-sections
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,-Map,${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/uniconnect.map
)

# 产物: bin(fw_update.py 消费)/hex + size(map 经 -Wl,-Map 落 build/)
add_custom_command(TARGET uniconnect.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:uniconnect.elf>
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/uniconnect.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:uniconnect.elf>
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/uniconnect.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:uniconnect.elf>
    COMMENT "== Generating uniconnect.bin/.hex =="
)
