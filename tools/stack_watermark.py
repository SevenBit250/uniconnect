#!/usr/bin/env python3
"""stack_watermark.py -- 线程栈高水位检测(RT-Thread '#' 预填特性)

原理: rt_thread_init 以 0x23('#') 预填栈, 栈向下生长, 脏区下界=历史最深
触底。水位单调保持, 故运行任意负载后扫描与实时检测等价(极限工况的最深
一次也会留痕)。栈真溢出时脏区会越过栈底, 同样能检出。

用法:
  python tools/stack_watermark.py                     # dump并按map解析当前水位
  python tools/stack_watermark.py --limit 60          # 额外要求利用率<60%
  python tools/stack_watermark.py --map build-rel/uniconnect.map  # 指定map(默认build/, 与设备固件必须同构建!)
输出: 各栈实占/利用率; 越限或覆盖不全退出码非0。
依赖: openocd(wch-link) + map 与设备固件同步(跨构建错配=地址全错, 读数无效)。
"""
import re
import subprocess
import sys
import os

OCD = r"C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\OpenOCD\OpenOCD\bin\openocd.exe"
CFG = r"C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\OpenOCD\OpenOCD\bin\wch-riscv.cfg"
MAP = os.path.join(os.path.dirname(__file__), "..", "build", "uniconnect.map")
# --map 覆盖(多构建目录: build/=Debug, build-rel/=Release)
_args = [a for a in sys.argv[1:] if not a.startswith("--limit")]
for _i, _a in enumerate(_args):
    if _a == "--map" and _i + 1 < len(_args):
        MAP = _args[_i + 1]

# 监视的栈符号(线程显示名 -> map符号)
STACKS = {
    "cdc_tx":    "cdc_tx_stack",
    "cdc_rx":    "cdc_rx_stack",
    "cdc_ctrl":  "cdc_ctrl_stack",
    "dap":       "dap_thread_stack",
    "monitor":   "monitor_thread_stack",
    "can_brg":   "can_bridge_stack",
    "idle":      "rt_thread_stack",
}

def load_map():
    text = open(MAP, encoding="utf-8", errors="replace").read()
    out = {}
    for disp, sym in STACKS.items():
        m = re.search(rf"\.[sb]ss\.{sym}\n\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)", text)
        if m:
            out[disp] = (int(m.group(1), 16), int(m.group(2), 16))
    return out

def dump(addr, words):
    r = subprocess.run([OCD, "-f", CFG, "-c", "init", "-c", "halt",
                        "-c", f"mdw 0x{addr:x} {words}", "-c", "resume", "-c", "shutdown"],
                       capture_output=True, text=True, timeout=60)
    mem = {}
    for line in (r.stdout + r.stderr).splitlines():   # openocd的mdw输出走stderr
        m = re.match(r"0x([0-9a-f]+):\s+((?:[0-9a-f]{8}\s*)+)", line.strip())
        if not m:
            continue
        a = int(m.group(1), 16)
        for i, w in enumerate(m.group(2).split()):
            mem[a + 4 * i] = int(w, 16)
    return mem

def main():
    limit = 60
    if "--limit" in sys.argv:
        limit = int(sys.argv[sys.argv.index("--limit") + 1])
    stacks = load_map()
    if not stacks:
        sys.exit("map解析失败(栈符号未找到, build/与固件同步?)")
    bad = []
    print(f"{'线程':10} {'分配':>6} {'实占':>6} {'利用率':>7}")
    for disp, (base, size) in sorted(stacks.items()):
        mem = dump(base, size // 4)
        covered = sum(1 for a in range(base, base + size, 4) if a in mem)
        deepest = base + size
        for a in range(base, base + size, 4):
            if a in mem and mem[a] != 0x23232323:
                deepest = a
                break
        used = (base + size) - deepest
        pct = used * 100 // size
        flag = ""
        if covered * 4 < size:
            flag = " [覆盖不全!]"
            bad.append(disp)
        elif pct >= limit:
            flag = f" [越限>{limit}%!]"
            bad.append(disp)
        print(f"{disp:10} {size:6} {used:6} {pct:6}%{flag}")
    if bad:
        sys.exit(f"FAIL: {bad}")
    print("PASS")

if __name__ == "__main__":
    main()
