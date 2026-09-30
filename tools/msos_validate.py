#!/usr/bin/env python3
"""msos_validate.py -- 校验 bsp_usb.c 里的 MS OS 2.0 描述符集与 BOS 长度一致性

背景: 该描述符集已两次因字节级错误被 Windows 判废(类型字段违反表9 /
BOS wMSOSDescriptorSetTotalLength 与集合 wTotalLength 不同步), 肉眼对账
不可靠, 本脚本把"排除结论必须带覆盖清单"做成机械检查。

用法: python tools/msos_validate.py
"""
import re
import sys

SRC = "Bsp/src/bsp_usb.c"

def extract_array(name):
    text = open(SRC, encoding="utf-8", errors="replace").read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)   # 去块注释(防其中数字污染字节流)
    text = re.sub(r"//[^\n]*", "", text)                # 去行注释
    m = re.search(rf"{name}\[\]\s*=\s*\{{(.*?)\}};", text, re.S)
    if not m:
        sys.exit(f"未找到数组 {name}")
    vals = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b", m.group(1))]
    return bytes(vals)

def u16(b, o): return b[o] | (b[o+1] << 8)
def u32(b, o): return b[o] | (b[o+1] << 8) | (b[o+2] << 16) | (b[o+3] << 24)

errors = []

msos = extract_array("msosv2_descriptor_set")
bos = extract_array("bos_descriptor")

print(f"MSOS set: {len(msos)} B, BOS: {len(bos)} B")

# ---- 集合头 ----
if u32(msos, 0) != 10: errors.append(f"集合头 dwLength={u32(msos,0)} != 10(固定)")
if u16(msos, 4) != 0x0000: errors.append(f"集合头 wDescriptorType={u16(msos,4):#x} != 0x0000")
wtotal = u16(msos, 8)
if wtotal != len(msos):
    errors.append(f"集合 wTotalLength={wtotal} != 实际长度 {len(msos)}")

# ---- 遍历功能子集, 必须精确平铺满 wTotalLength ----
off = 10
subsets = []
while off < len(msos):
    if off + 8 > len(msos):
        errors.append(f"偏移{off}: 子集头越界(集合尾部未平铺)"); break
    dlen = u16(msos, off)
    dtype = u16(msos, off + 2)
    first_if = msos[off + 4]
    sublen = u16(msos, off + 6)
    if dtype != 0x0002:
        errors.append(f"偏移{off}: 子集头类型 {dtype:#x} != 0x0002(功能子集)")
    if dlen != 8:
        errors.append(f"偏移{off}: 子集头 dwLength={dlen} != 8(固定)")
    if off + sublen > len(msos):
        errors.append(f"偏移{off}: 子集长度 {sublen} 越界(剩 {len(msos)-off})"); break
    subsets.append((first_if, off, sublen))
    # 子集内部: 平铺 兼容ID(0x03)/注册表属性(0x04)
    so = off + 8
    while so < off + sublen:
        if so + 4 > off + sublen:
            errors.append(f"子集if{first_if} 偏移{so}: 子描述符头越界"); break
        slen = u16(msos, so); stype = u16(msos, so + 2)
        if stype not in (0x0003, 0x0004):
            errors.append(f"子集if{first_if} 偏移{so}: 子描述符类型 {stype:#x} ∉ {{0x03兼容ID, 0x04注册表属性}}")
        if slen < 4 or so + slen > off + sublen:
            errors.append(f"子集if{first_if} 偏移{so}: 子描述符长度 {slen} 越界"); break
        if stype == 0x0003 and slen != 20:
            errors.append(f"子集if{first_if}: 兼容ID描述符长度 {slen} != 20(固定)")
        if stype == 0x0004:
            # 注册表属性内部长度自洽: wLength 覆盖整个描述符
            if slen != 132:
                print(f"  注: 子集if{first_if} 注册表属性长度 {slen} (非132, 若含GUID数组属正常)")
        so += slen
    if so != off + sublen:
        errors.append(f"子集if{first_if}: 内部平铺 {so-off} != 子集长度 {sublen}")
    off += sublen
if off != len(msos):
    errors.append(f"子集总平铺 {off} != 集合长度 {len(msos)}")

# ---- BOS 内 wMSOSDescriptorSetTotalLength 必须等于集合 wTotalLength ----
# BOS布局: 头5B + 平台能力28B: bLength@5, bcdVersion@7(2B), UUID@9(16B),
# 版本@25(4B), 集合总长@29(2B), vendor@31, alt@32
if len(bos) != 33: errors.append(f"BOS 长度 {len(bos)} != 33")
bos_total = u16(bos, 29)
if bos_total != wtotal:
    errors.append(f"BOS wMSOSDescriptorSetTotalLength={bos_total} != 集合 wTotalLength={wtotal} (不同步, Windows判废)")
if bos[5] != 0x1C: errors.append(f"BOS 平台能力 bLength={bos[5]:#x} != 0x1C(28)")
if u32(bos, 25) != 0x06030000:
    errors.append(f"BOS dwWindowsVersion={u32(bos,25):#x} != 0x06030000 (Win8.1+)")

# ---- 输出 ----
print(f"子集: {[(f'if{i}', f'off{o}', f'len{l}') for i, o, l in subsets]}")
print(f"集合 wTotalLength={wtotal}, BOS 同步字段={bos_total}")

if errors:
    print("\n== FAIL ==")
    for e in errors: print(" -", e)
    sys.exit(1)
print("\n== PASS ==")
