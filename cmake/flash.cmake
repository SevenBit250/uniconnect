# 烧录目标: flash_app / flash_boot / flash_all (wch-link + openocd)
#
# [本仓教训, 勿再踩]
# 1. 烧录后若 Windows 不重枚举 USB: openocd reset 不复位外设, 需物理断电重插(POR)
# 2. 禁止 openocd `flash erase_address` -- WCH SDI 驱动上是整片擦(实锚, 2026-10-03)
# 3. flash 操作后不要 resume -- FLASH_CTLR 脏态+固件执行=整片擦除, program+reset run 安全

if(DEFINED ENV{OPENOCD_BIN})
    set(OPENOCD_BIN "$ENV{OPENOCD_BIN}")
else()
    set(OPENOCD_BIN "C:/MounRiver/MounRiver_Studio2/resources/app/resources/win32/components/WCH/OpenOCD/OpenOCD/bin")
endif()
set(OPENOCD_EXE ${OPENOCD_BIN}/openocd.exe)
set(OPENOCD_CFG ${OPENOCD_BIN}/wch-riscv.cfg)

set(APP_ELF ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/uniconnect.elf)
set(BOOT_ELF ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/boot.elf)

add_custom_target(flash_app
    COMMAND ${OPENOCD_EXE} -f "${OPENOCD_CFG}"
            -c "program ${APP_ELF} verify" -c "reset run" -c shutdown
    DEPENDS uniconnect.elf
    COMMENT "== Flashing APP (uniconnect.elf) =="
    VERBATIM
)

add_custom_target(flash_boot
    COMMAND ${OPENOCD_EXE} -f "${OPENOCD_CFG}"
            -c "program ${BOOT_ELF} verify" -c "reset run" -c shutdown
    DEPENDS boot.elf
    COMMENT "== Flashing BOOT (boot.elf) =="
    VERBATIM
)

# 双镜像一次会话烧录(boot 在前, APP 在后, 同日常 openocd 手工流程)
add_custom_target(flash_all
    COMMAND ${OPENOCD_EXE} -f "${OPENOCD_CFG}"
            -c "program ${BOOT_ELF} verify"
            -c "program ${APP_ELF} verify"
            -c "reset run" -c shutdown
    DEPENDS boot.elf uniconnect.elf
    COMMENT "== Flashing BOOT + APP =="
    VERBATIM
)
