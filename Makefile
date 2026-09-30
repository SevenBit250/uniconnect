# 根 Makefile — 由 gen_makefile.sh 生成, 转发到 obj/GNUmakefile (先运行该脚本)
#   make           : 完整构建, 产物收集到 build/ (elf/bin/hex/map)
#   make flash     : openocd + wch-link 烧录
#   make clean     : MRS 原生清理; make clean-all 额外删除 build/
.PHONY: all flash stage clean clean-all
all flash stage clean clean-all:
	$(MAKE) -C obj -f GNUmakefile $@
