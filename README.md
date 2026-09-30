# UniConnect — 带 CAN 的 CMSIS-DAP 探针

基于 CH32V305CCT6（RISC-V，主频 144MHz）的调试探针固件：SWD 调试 + CDC 虚拟串口 + 双路 CAN-USB 桥，运行 RT-Thread Nano。

## 功能进度

- [x] USB 复合设备通信（CDC + CAN WinUSB，MS OS 2.0 免驱绑定已真机验证）
- [x] CDC 虚拟串口（921600–9M 全档位零丢失）
- [x] 双路 CAN + CAN-USB 桥（1M 波特率，双路 1000 帧回环 PASS）
- [ ] SWD 调试（DAP 配置进行中）

## 首次编译

### 前置条件

- [MounRiver Studio](http://www.mounriver.com/)（MRS）≥ 2.3，默认安装于 `C:\MounRiver\MounRiver_Studio2`，提供 RISC-V 工具链、make 与 openocd；装在别处时用环境变量 `MRS_HOME` 覆盖（见下）
- Git Bash（运行 `gen_makefile.sh` 需要 bash）
- 烧录需要 WCH-Link / WCH-LinkE 调试器

### 步骤

1. **MRS 里构建一次**：导入本工程后 Build（Ctrl+B）。命令行构建依赖 IDE 生成的 `obj/makefile` 与各 `subdir.mk`，每台机器只需做一次。

2. **生成自己的构建入口**（仓库根目录执行）：

   ```bash
   bash gen_makefile.sh
   ```

   产出两个文件：`obj/GNUmakefile`（完整继承 MRS makefile，注入工具链 PATH、修正链接脚本绝对路径，末尾增补产物收集与烧录目标）和根目录 `Makefile`（转发层）。

3. **把 make 加进 PATH**（Git Bash）：

   ```bash
   export PATH="/c/MounRiver/MounRiver_Studio2/resources/app/resources/win32/others/Build_Tools/Make/bin:$PATH"
   ```

4. **编译与烧录**（仓库根目录）：

   ```bash
   make            # 完整构建，产物收集到 build/：uniconnect.elf / .bin / .hex / .map
   make flash      # openocd + wch-link 烧录（program elf verify 后复位运行）
   make clean      # MRS 原生清理；make clean-all 额外删除 build/
   ```

### 备注

- 仓库被移动后：重跑 `bash gen_makefile.sh` 即自愈 `obj/GNUmakefile` 里的 Link.ld 绝对路径。
- MRS 不在默认位置时：`MRS_HOME="/d/your/mrs/path" bash gen_makefile.sh` 重新生成（也可单独覆盖 `TOOLCHAIN_BIN` / `OPENOCD_BIN`）。
- Windows 原生 make 的 `$(PATH)` 恒为分号形式，makefile 内注入 PATH 必须用 `C:/` 形式路径并探测分隔符——生成器已处理，勿手改，细节见 AGENTS.md 已知坑。

## 更多文档

构建细节、目录分层约定与已知坑（DMA 狂奔、HPE 硬件栈、MS OS 2.0 免驱排查史等）见 [AGENTS.md](AGENTS.md)。
