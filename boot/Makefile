# UniConnect Bootloader 独立构建(裸机, 20KB分区)
# 共享主工程的厂商库/CherryUSB/启动文件源码, 独立obj与链接脚本。
# 产物: ../build/boot.{elf,bin,map}
#
# 用法: cd boot && make   (或仓库根 make -C boot)

.DEFAULT_GOAL := all

# 原生Windows make的$(PATH)恒为分号形式(AGENTS已知坑): findstring探测分隔符再拼接
TOOLBIN := C:/MounRiver/MounRiver_Studio2/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC/bin
ifeq ($(findstring ;,$(PATH)),)
export PATH := $(TOOLBIN):$(PATH)
else
export PATH := $(TOOLBIN);$(PATH)
endif
CC      := riscv-none-embed-gcc
OBJCOPY := riscv-none-embed-objcopy
SIZE    := riscv-none-embed-size

ARCH    := -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore
CFLAGS  := $(ARCH) -Os -ffunction-sections -fdata-sections -fno-common \
           -fmessage-length=0 -fsigned-char -Wall \
           -DCONFIG_USB_BAREMETAL -DCONFIG_USB_DBG_LEVEL=USB_DBG_DISABLE \
           -IUser -I../User -I../Core -I../Peripheral/inc \
           -I../CherryUSB -I../CherryUSB/core -I../CherryUSB/common \
           -I../CherryUSB/class/hid -I../CherryUSB/port/ch32/ch32hs
LDFLAGS := $(ARCH) -T Link.ld -nostartfiles -Xlinker --gc-sections \
           --specs=nano.specs --specs=nosys.specs -Wl,-Map,boot.map \
           -Wl,--print-memory-usage

C_SRCS := \
  User/main.c \
  User/boot_it.c \
  User/bootproto.c \
  User/flash_io.c \
  User/usb_glue.c \
  ../Core/core_riscv.c \
  User/system_ch32v30x.c \
  ../Peripheral/src/ch32v30x_rcc.c \
  ../Peripheral/src/ch32v30x_gpio.c \
  ../Peripheral/src/ch32v30x_flash.c \
  ../Peripheral/src/ch32v30x_misc.c \
  ../CherryUSB/core/usbd_core.c \
  ../CherryUSB/class/hid/usbd_hid.c \
  ../CherryUSB/port/ch32/ch32hs/usb_dc_usbhs.c

S_SRCS := ../Startup/startup_ch32v30x_D8.S

OBJS := $(addprefix build/,$(notdir $(C_SRCS:.c=.o) $(S_SRCS:.S=.o)))

build:
	@mkdir -p build

$(OBJS): | build

# 用路径全展开防同名冲突(目前无同名, 逐文件规则足够)
build/main.o: User/main.c
	@$(CC) $(CFLAGS) -c $< -o $@
build/boot_it.o: User/boot_it.c
	@$(CC) $(CFLAGS) -c $< -o $@
build/bootproto.o: User/bootproto.c
	@$(CC) $(CFLAGS) -c $< -o $@
build/flash_io.o: User/flash_io.c
	@$(CC) $(CFLAGS) -c $< -o $@
build/usb_glue.o: User/usb_glue.c
	@$(CC) $(CFLAGS) -c $< -o $@
build/core_riscv.o: ../Core/core_riscv.c
	@$(CC) $(CFLAGS) -c $< -o $@
build/system_ch32v30x.o: User/system_ch32v30x.c
	@$(CC) $(CFLAGS) -c $< -o $@
build/ch32v30x_rcc.o: ../Peripheral/src/ch32v30x_rcc.c
	@$(CC) $(CFLAGS) -c $< -o $@
build/ch32v30x_gpio.o: ../Peripheral/src/ch32v30x_gpio.c
	@$(CC) $(CFLAGS) -c $< -o $@
build/ch32v30x_misc.o: ../Peripheral/src/ch32v30x_misc.c
	@$(CC) $(CFLAGS) -c $< -o $@
build/ch32v30x_flash.o: ../Peripheral/src/ch32v30x_flash.c
	@$(CC) $(CFLAGS) -c $< -o $@
build/usbd_core.o: ../CherryUSB/core/usbd_core.c
	@$(CC) $(CFLAGS) -c $< -o $@
build/usbd_hid.o: ../CherryUSB/class/hid/usbd_hid.c
	@$(CC) $(CFLAGS) -c $< -o $@
build/usb_dc_usbhs.o: ../CherryUSB/port/ch32/ch32hs/usb_dc_usbhs.c
	@$(CC) $(CFLAGS) -c $< -o $@
build/startup_ch32v30x_D8.o: ../Startup/startup_ch32v30x_D8.S
	@$(CC) $(ARCH) -c $< -o $@

boot.elf: $(OBJS) Link.ld
	@$(CC) $(LDFLAGS) -o boot.elf $(OBJS)

all: boot.elf
	@$(OBJCOPY) -O binary boot.elf ../build/boot.bin
	@$(OBJCOPY) -O ihex boot.elf ../build/boot.hex
	@$(SIZE) --format=berkeley boot.elf

flash: all
	@echo "烧录boot: openocd -c 'flash probe 0' ... 使用主工程 make flash 流程手动烧"

clean:
	@rm -rf build boot.elf boot.map ../build/boot.bin ../build/boot.hex

.PHONY: all flash clean
