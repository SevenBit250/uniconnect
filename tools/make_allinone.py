#!/usr/bin/env python3
"""make_allinone.py -- 工厂整片镜像合成: boot@0x0 + APP@0x5000 -> 256KB all_in_one

分区事实源 common/boot_shared.h + boot/User/boot.h:
  boot 0x0000..0x4FFF (20KB), APP 0x5000..0x3EFFF (228KB), 旗标页 0x3F000 (4KB, 保持擦除态0xFF)
用法: python tools/make_allinone.py <boot.bin> <app.bin> <out.bin> [out.hex]
校验: boot不得越界入APP区, APP不得超APP_MAX_SIZE; 旗标页强制0xFF(出厂=无GOOD记录,
      首次上电boot验签后放行, 或走 fw_update 一次以建立记录 -- 见README出厂流程)。
"""
import sys

FLASH_SIZE = 256 * 1024
APP_BASE = 0x5000
APP_MAX = FLASH_SIZE - 0x1000 - APP_BASE
FLAG_PAGE = FLASH_SIZE - 0x1000


def to_ihex(data: bytes, base: int, path: str):
    """Intel HEX(16B记录, 线性地址扩展) -- wch-link/OpenOCD 工厂烧录通用格式"""
    lines = []

    def ext_line(hi16: int):
        rec = bytes([2, 0, 0, 4, (hi16 >> 8) & 0xFF, hi16 & 0xFF])
        return ":" + rec.hex().upper() + _cksum(rec)

    cur_hi = -1
    for i in range(0, len(data), 16):
        addr = base + i
        hi = addr >> 16
        if hi != cur_hi:
            lines.append(ext_line(hi))
            cur_hi = hi
        chunk = data[i:i + 16]
        rec = bytes([len(chunk), (addr >> 8) & 0xFF, addr & 0xFF, 0]) + chunk
        lines.append(":" + rec.hex().upper() + _cksum(rec))
    lines.append(":00000001FF")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def _cksum(rec: bytes) -> str:
    return f"{(-sum(rec)) & 0xFF:02X}"


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    boot = open(sys.argv[1], "rb").read()
    app = open(sys.argv[2], "rb").read()
    if len(boot) > APP_BASE:
        print(f"ERROR: boot {len(boot)}B 越界(>{APP_BASE}B)")
        return 1
    if len(app) > APP_MAX:
        print(f"ERROR: app {len(app)}B 超限(>{APP_MAX}B)")
        return 1

    img = bytearray(b"\xFF" * FLASH_SIZE)
    img[0:len(boot)] = boot
    img[APP_BASE:APP_BASE + len(app)] = app
    # 旗标页保持0xFF(擦除态): 出厂即"无GOOD记录", 由首刷流程或APP签名放行语义接管
    img[FLAG_PAGE:] = b"\xFF" * (FLASH_SIZE - FLAG_PAGE)
    open(sys.argv[3], "wb").write(bytes(img))

    if len(sys.argv) > 4:
        to_ihex(bytes(img), 0, sys.argv[4])

    print(f"all_in_one: boot={len(boot)}B@0x0 app={len(app)}B@0x{APP_BASE:X} "
          f"total={len(img)}B -> {sys.argv[3]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
