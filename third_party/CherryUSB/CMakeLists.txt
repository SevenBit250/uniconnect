# vendored CherryUSB v1.51(device-only, ch32hs 端口), 模块化接口源集
# 官方仓库无 ch32 移植层的 CMake 支持(cherryusb.cmake 不含 ch32), 此为自建等价物:
# 每个类驱动/端口一个目标, 按需链接 -- boot 只链 core+hid, APP 链全家

set(CHERRYUSB_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

add_library(cherryusb_core INTERFACE)
target_sources(cherryusb_core INTERFACE ${CHERRYUSB_ROOT}/core/usbd_core.c)
target_include_directories(cherryusb_core INTERFACE
    ${CHERRYUSB_ROOT}            # usb_config.h(项目级配置)
    ${CHERRYUSB_ROOT}/common
    ${CHERRYUSB_ROOT}/core
)

add_library(cherryusb_cdc INTERFACE)
target_sources(cherryusb_cdc INTERFACE ${CHERRYUSB_ROOT}/class/cdc/usbd_cdc_acm.c)
target_include_directories(cherryusb_cdc INTERFACE ${CHERRYUSB_ROOT}/class/cdc)

add_library(cherryusb_hid INTERFACE)
target_sources(cherryusb_hid INTERFACE ${CHERRYUSB_ROOT}/class/hid/usbd_hid.c)
target_include_directories(cherryusb_hid INTERFACE ${CHERRYUSB_ROOT}/class/hid)

add_library(cherryusb_dfu INTERFACE)
target_sources(cherryusb_dfu INTERFACE ${CHERRYUSB_ROOT}/class/dfu/usbd_dfu.c)
target_include_directories(cherryusb_dfu INTERFACE ${CHERRYUSB_ROOT}/class/dfu)

add_library(cherryusb_msc INTERFACE)
target_sources(cherryusb_msc INTERFACE ${CHERRYUSB_ROOT}/class/msc/usbd_msc.c)
target_include_directories(cherryusb_msc INTERFACE ${CHERRYUSB_ROOT}/class/msc)

# RT-Thread osal(仅 APP; boot 裸机零 osal -- CONFIG_USB_BAREMETAL + usb_glue 泵)
add_library(cherryusb_osal_rtthread INTERFACE)
target_sources(cherryusb_osal_rtthread INTERFACE ${CHERRYUSB_ROOT}/osal/usb_osal_rtthread.c)
target_include_directories(cherryusb_osal_rtthread INTERFACE ${CHERRYUSB_ROOT}/osal)

# ch32hs 设备端口(仅 APP 链接此目标)
# [双编译约定] boot 不链本目标, 而是直接 target_sources 同一 .c 并 PRIVATE 定义
# CONFIG_USB_BAREMETAL(剥离 rtthread.h/中断嵌套计数), 见 boot/CMakeLists.txt
add_library(cherryusb_ch32hs INTERFACE)
target_sources(cherryusb_ch32hs INTERFACE ${CHERRYUSB_ROOT}/port/ch32/ch32hs/usb_dc_usbhs.c)
target_include_directories(cherryusb_ch32hs INTERFACE ${CHERRYUSB_ROOT}/port/ch32/ch32hs)
