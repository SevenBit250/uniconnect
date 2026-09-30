#!/bin/bash
# gen_makefile.sh — 从 MRS 自动生成的 obj/makefile 派生我们自己的构建入口
#
# 产物(每次运行重新生成, 可重复执行):
#   obj/GNUmakefile  : 完整继承 MRS makefile(仅修正 Link.ld 绝对路径), 末尾增补:
#                      1) 工具链 PATH 注入(MRS 以裸命令名调用 riscv-none-embed-*)
#                      2) stage: 核心产物 elf/bin/hex/map 收集到仓库根 build/
#                         (bin 不在 MRS 产物之列, 由 elf 经 objcopy 现场生成)
#                      3) flash: openocd + wch-link 烧录(program elf verify + 复位运行)
#   Makefile (根目录): 转发层, 让仓库根直接 make / make flash / make clean
#
# 增补逻辑固化在生成的 makefile 里(本脚本只做拼接), 仓库被移动后重跑本脚本即可
# 自愈 Link.ld 绝对路径。
#
# 前提: MRS 至少构建过一次(obj/makefile 与各 subdir.mk 存在)。
# 用法: bash gen_makefile.sh
#       可用环境变量覆盖默认路径: MRS_HOME / TOOLCHAIN_BIN / OPENOCD_BIN

set -euo pipefail

# 仓库根 = 脚本所在目录(POSIX 路径用于文件操作, Windows 形式用于烧录命令)
ROOT="$(cd "$(dirname "$0")" && pwd)"
ROOT_W="$(cd "$(dirname "$0")" && pwd -W 2>/dev/null || pwd)"

# ---- 默认路径(可用环境变量覆盖) ----
MRS_HOME="${MRS_HOME:-/c/MounRiver/MounRiver_Studio2/resources/app/resources/win32}"
TOOLCHAIN_BIN="${TOOLCHAIN_BIN:-$MRS_HOME/components/WCH/Toolchain/RISC-V Embedded GCC/bin}"
OPENOCD_BIN="${OPENOCD_BIN:-$MRS_HOME/components/WCH/OpenOCD/OpenOCD/bin}"

SRC="$ROOT/obj/makefile"
DST="$ROOT/obj/GNUmakefile"

[ -f "$SRC" ] || { echo "错误: 未找到 $SRC — 先在 MRS 里构建一次" >&2; exit 1; }
[ -x "$TOOLCHAIN_BIN/riscv-none-embed-gcc" ] || echo "警告: 工具链未找到: $TOOLCHAIN_BIN" >&2
[ -x "$OPENOCD_BIN/openocd.exe" ] || echo "警告: openocd 未找到: $OPENOCD_BIN" >&2

# 1) 继承 MRS makefile, 仅把链接脚本的绝对路径修正为当前仓库位置
#    (原生 gcc 不认 /c/... 形式, 用 pwd -W 的 C:/... 形式)
sed -e "s|[A-Za-z]:\\\\[^\" ]*Link\.ld|$ROOT_W/Ld/Link.ld|g" "$SRC" > "$DST"

# 工具路径的 Windows 正斜杠形式(原生 make/sh 只认这种, 实测 /c/ 形式会拼出
# 混合分隔符的废 PATH: Git Bash 启动原生 make 时 $(PATH) 已被 MSYS 转成分号形式)
to_win() { cygpath -m "$1" 2>/dev/null || echo "$1" | sed -e 's|^/\([a-zA-Z]\)/|\U\1:/|'; }
TOOLCHAIN_BIN_W="$(to_win "$TOOLCHAIN_BIN")"
OPENOCD_BIN_W="$(to_win "$OPENOCD_BIN")"

# 2) 追加增补段(引号定界符避免 shell 展开, @占位符@ 事后替换)
cat >> "$DST" <<'EOF'

# =====================================================================
# UniConnect 增补段 (gen_makefile.sh 生成, 勿手改 — 重跑脚本重新生成)
# =====================================================================

# 工具链注入: MRS 规则以裸命令名调用 riscv-none-embed-*
# 注意: 原生 Windows make 的 $(PATH) 恒为分号形式(Git Bash 启动时被 MSYS
# 转换), 必须按探测到的分隔符风格拼接, 且路径用 C:/ 正斜杠形式
ifeq ($(findstring ;,$(PATH)),)
export PATH := @TOOLCHAIN_BIN_W@:$(PATH)
else
export PATH := @TOOLCHAIN_BIN_W@;$(PATH)
endif

# ---- 产物收集: elf/bin/hex/map 复制到仓库根 build/ ----
BUILD_DIR := ../build
TARGET := uniconnect

.PHONY: stage flash clean-all
main-build: stage

stage: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin \
        $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).map

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/$(TARGET).elf: $(TARGET).elf | $(BUILD_DIR)
	cp $(TARGET).elf $@

# bin 不在 MRS 构建产物之列, 由 elf 现场生成
$(BUILD_DIR)/$(TARGET).bin: $(TARGET).elf | $(BUILD_DIR)
	riscv-none-embed-objcopy -O binary $(TARGET).elf $@

$(BUILD_DIR)/$(TARGET).hex: $(TARGET).hex | $(BUILD_DIR)
	cp $(TARGET).hex $@

$(BUILD_DIR)/$(TARGET).map: $(TARGET).map | $(BUILD_DIR)
	cp $(TARGET).map $@

# ---- 烧录: wch-link + openocd, program elf verify 后复位运行 ----
OPENOCD := @OPENOCD_BIN_W@/openocd.exe
OPENOCD_CFG := @OPENOCD_BIN_W@/wch-riscv.cfg

flash: $(TARGET).elf
	$(OPENOCD) -f "$(OPENOCD_CFG)" -c "program $(TARGET).elf verify" -c "reset run" -c "shutdown"

# clean-all: 在 MRS clean 基础上连 build/ 一起删
clean-all: clean
	-$(RM) $(BUILD_DIR)
EOF

sed -i -e "s|@TOOLCHAIN_BIN_W@|$TOOLCHAIN_BIN_W|g" -e "s|@OPENOCD_BIN_W@|$OPENOCD_BIN_W|g" "$DST"

# 3) 根目录转发 Makefile(此文件可提交; obj/ 不被跟踪, GNUmakefile 随脚本再生)
cat > "$ROOT/Makefile" <<'EOF'
# 根 Makefile — 由 gen_makefile.sh 生成, 转发到 obj/GNUmakefile (先运行该脚本)
#   make           : 完整构建, 产物收集到 build/ (elf/bin/hex/map)
#   make flash     : openocd + wch-link 烧录
#   make clean     : MRS 原生清理; make clean-all 额外删除 build/
.PHONY: all flash stage clean clean-all
all flash stage clean clean-all:
	$(MAKE) -C obj -f GNUmakefile $@
EOF

echo "已生成:"
echo "  $DST"
echo "  $ROOT/Makefile"
echo "用法(仓库根目录): make / make flash / make clean"
echo "提示: make 本体不在 PATH 时: export PATH=\"$MRS_HOME/others/Build_Tools/Make/bin:\$PATH\""
