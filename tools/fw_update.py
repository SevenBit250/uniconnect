#!/usr/bin/env python3
"""fw_update.py -- UniConnect 固件升级工具(两阶段, docs/bootloader-design.md)

阶段1(APP态): WinUSB接口2(DAP)发 CMSIS-DAP 厂商命令 Vendor0(0x80)
             -> 设备写升级旗标+复位, APP消失
阶段2(boot态): 等待 HID 设备出现(VID=0x0D28 PID=0x0219 "UniConnect Bootloader")
             -> GET_INFO/BEGIN/PROGRAM/FLUSH/VERIFY/END/REBOOT 传镜像

用法: python tools/fw_update.py build/uniconnect.bin [--skip-trigger] [--info]
  --skip-trigger  设备已在bootloader(如升级中断后重试), 跳过阶段1
"""
import sys
import time
import struct
import argparse

import usb.core
import usb.backend.libusb1

VID, PID_APP, PID_BOOT = 0x0D28, 0x0218, 0x0219
DAP_IF_NUM, DAP_OUT, DAP_IN = 2, 0x02, 0x81
DLL = (r'C:\Users\25293\AppData\Local\Programs\Python\Python312\Lib'
       r'\site-packages\libusb\_platform\windows\x86_64\libusb-1.0.dll')

# 协议命令字(与 boot/User/boot.h 严格同步)
BPC_GET_INFO, BPC_BEGIN, BPC_PROGRAM, BPC_FLUSH = 0x01, 0x02, 0x03, 0x04
BPC_VERIFY, BPC_END, BPC_REBOOT, BPC_ERASE_FLAG = 0x05, 0x06, 0x07, 0x08
BP_PAYLOAD_MAX = 56

STATUS_NAME = {0: 'OK', 1: 'ERR', 2: 'STATE', 3: 'CRC', 4: 'OVERFLOW'}


def crc32(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1))
    return crc ^ 0xFFFFFFFF


class HidBoot:
    def __init__(self):
        import hid
        self.hid = hid
        self.dev = None

    def open(self, timeout_s=15.0):
        t0 = time.time()
        while time.time() - t0 < timeout_s:
            for d in self.hid.enumerate(VID, PID_BOOT):
                self.dev = self.hid.device()
                self.dev.open_path(d['path'])
                return self
            time.sleep(0.3)
        raise RuntimeError('bootloader HID设备未出现 (VID=%04X PID=%04X)' % (VID, PID_BOOT))

    def xfer(self, cmd, seq, data=b'', addr=0):
        pkt = bytearray(64)
        pkt[0] = cmd
        pkt[1] = seq
        pkt[2] = len(data)
        pkt[3:7] = struct.pack('<I', addr)
        pkt[7:7 + len(data)] = data
        # hidapi在Windows要求缓冲前置report-id字节(无ID时补0x00);
        # 读回同样带此前缀, 取末64B对齐
        self.dev.write(b'\x00' + bytes(pkt))
        rsp = None
        for _ in range(50):
            r = self.dev.read(65, timeout_ms=200)
            if r and len(r) >= 64:
                r = r[-64:]
            if r and (r[0] & 0x7F) == cmd and r[1] == seq:
                rsp = r
                break
        if rsp is None:
            raise RuntimeError(f'命令0x{cmd:02X} 无应答')
        return rsp

    def close(self):
        if self.dev:
            self.dev.close()


def trigger_via_dap():
    be = usb.backend.libusb1.get_backend(find_library=lambda x: DLL)
    dev = usb.core.find(idVendor=VID, idProduct=PID_APP, backend=be)
    if dev is None:
        raise RuntimeError('APP设备未找到(VID=%04X PID=%04X)' % (VID, PID_APP))
    dev.set_configuration()
    cfg = dev.get_active_configuration()
    usb.util.claim_interface(dev, DAP_IF_NUM)
    pkt = bytes([0x80]) + b'\x00' * 63          # ID_DAP_Vendor0
    dev.write(DAP_OUT, pkt, 1000)
    rsp = dev.read(DAP_IN, 64, 2000)
    usb.util.dispose_resources(dev)
    if not rsp or rsp[0] != 0x80 or rsp[1] != 0x00:
        raise RuntimeError(f'Vendor0应答异常: {list(rsp[:4]) if rsp else None}')
    print('  阶段1: 升级旗标已写入, 设备复位中...')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('image', nargs='?')
    ap.add_argument('--skip-trigger', action='store_true')
    ap.add_argument('--info', action='store_true', help='仅读取bootloader信息')
    args = ap.parse_args()

    if args.info:
        b = HidBoot().open(3.0)
        try:
            r = b.xfer(BPC_GET_INFO, 0)
            ver = r[3]
            flash_kb = r[4] | (r[5] << 8)
            app_base = r[7] | (r[8] << 8) | (r[9] << 16)
            max_img = r[11] | (r[12] << 8) | (r[13] << 16)
            print(f'bootloader: proto=v{ver} flash={flash_kb}KB app_base=0x{app_base:X} max_img={max_img}')
        finally:
            b.close()
        return

    if not args.image:
        ap.error('需要镜像路径 (build/uniconnect.bin)')

    image = open(args.image, 'rb').read()
    if len(image) > 237568:
        raise SystemExit(f'镜像超长: {len(image)} > 237568')
    crc = crc32(image)
    print(f'镜像: {args.image} {len(image)}B crc32=0x{crc:08X}')

    if not args.skip_trigger:
        trigger_via_dap()

    print('  阶段2: 等待bootloader...')
    b = HidBoot().open()
    seq = 0
    try:
        r = b.xfer(BPC_GET_INFO, seq); seq = (seq + 1) & 0xFF
        assert r[2] == 0, STATUS_NAME.get(r[2])

        r = b.xfer(BPC_BEGIN, seq, struct.pack('<II', len(image), crc), 0x5000); seq = (seq + 1) & 0xFF
        assert r[2] == 0, STATUS_NAME.get(r[2])
        print(f'  传输: {len(image)}B ...')

        t0 = time.time()
        addr = 0x5000
        off = 0
        while off < len(image):
            # 按页边界分包: 固件页缓冲要求单包不跨256B页界(56不能整除256)
            page_left = 256 - (addr & 0xFF)
            n = min(BP_PAYLOAD_MAX, page_left, len(image) - off)
            chunk = image[off:off + n]
            r = b.xfer(BPC_PROGRAM, seq, chunk, addr); seq = (seq + 1) & 0xFF
            if r[2] != 0:
                raise RuntimeError(f'PROGRAM@0x{addr:X}失败: {STATUS_NAME.get(r[2])}')
            addr += n
            off += n
            if off % 8190 < BP_PAYLOAD_MAX:      # 进度条(稀疏打印)
                print(f'\r  {off*100//len(image)}%', end='', flush=True)
        r = b.xfer(BPC_FLUSH, seq); seq = (seq + 1) & 0xFF
        assert r[2] == 0, STATUS_NAME.get(r[2])
        print(f'\r  传输完成, 用时{time.time()-t0:.1f}s')

        r = b.xfer(BPC_VERIFY, seq); seq = (seq + 1) & 0xFF
        if r[2] != 0:
            raise RuntimeError('VERIFY CRC失败: ' + STATUS_NAME.get(r[2], hex(r[2])))
        print('  VERIFY: CRC32校验通过')

        r = b.xfer(BPC_END, seq); seq = (seq + 1) & 0xFF
        assert r[2] == 0, STATUS_NAME.get(r[2])

        r = b.xfer(BPC_REBOOT, seq); seq = (seq + 1) & 0xFF
        assert r[2] == 0, STATUS_NAME.get(r[2])
        print('  REBOOT: 跳转APP, 升级完成')
    finally:
        b.close()


if __name__ == '__main__':
    main()
