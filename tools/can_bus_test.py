#!/usr/bin/env python3
"""can_bus_test.py -- 真实总线跨通道回环测试(CAN1/CAN2经收发器对接)

与can_usb_test.py(静默回环自测)不同: 两通道均设为正常模式(NORMAL),
CH1发帧->CH2线上接收验证, 再反向, 校验内容与序列完整性。
这是唯一验证TX/RX引脚映射与收发器硬件的测试。

用法: python tools/can_bus_test.py [帧数/方向] [波特率kbps]
"""
import sys
import time
import struct
import usb.core
import usb.util
import usb.backend.libusb1

VID, PID = 0x0D28, 0x0218
IF_NUM, EP_OUT, EP_IN = 3, 0x06, 0x83
DLL = r'C:\Users\25293\AppData\Local\Programs\Python\Python312\Lib\site-packages\libusb\_platform\windows\x86_64\libusb-1.0.dll'
CANP_TX, CANP_CTRL, CANP_RX, CANP_TXACK, CANP_EVENT, CANP_CTRL_RSP = 0x01, 0x02, 0x11, 0x13, 0x14, 0x15
CANC_SET_BITRATE, CANC_SET_MODE, CANC_GET_STATUS = 0x02, 0x03, 0x04
CAN_MODE_NORMAL = 1

be = usb.backend.libusb1.get_backend(find_library=lambda x: DLL)


class Dev:
    def __init__(self):
        self.dev = usb.core.find(idVendor=VID, idProduct=PID, backend=be)
        if self.dev is None:
            raise RuntimeError('设备未找到')
        cfg = self.dev.get_active_configuration()
        self.intf = cfg[(IF_NUM, 0)]
        usb.util.claim_interface(self.dev, IF_NUM)
        self.rx_frames = {0: [], 1: []}
        self.acks = {}
        self.last_ctrl_rsp = None

    def out(self, data):
        self.dev.write(EP_OUT, data, 1000)

    def ctrl(self, ch, cmd, payload=b''):
        import threading
        t = threading.Thread(target=self.pump, args=(0.3,))
        t.start()
        self.out(bytes([CANP_CTRL, ch, 0, 0, 1 + len(payload), 0, 0, 0]) + bytes([cmd]) + payload)
        t.join()

    def send_frames(self, ch, frames):
        pl = b''
        for (fid, flags, dlc, handle, data) in frames:
            pl += struct.pack('<IBBH8s', fid, flags, dlc, handle, data)
        n = len(frames)
        self.out(bytes([CANP_TX, ch, 0, 0, n & 0xFF, n >> 8, 0, 0]) + pl)

    def pump(self, duration=0.3):
        end = time.time() + duration
        while time.time() < end:
            try:
                data = self.dev.read(EP_IN, 512, 50)
            except usb.core.USBError:
                continue
            off = 0
            while off + 8 <= len(data):
                t, ch = data[off], data[off+1]
                count = data[off+4] | (data[off+5] << 8)
                if t == CANP_RX:
                    plen = count * 20
                elif t == CANP_TXACK:
                    plen = count * 4
                elif t == CANP_EVENT:
                    plen = count * 8
                elif t == CANP_CTRL_RSP:
                    plen = count
                else:
                    break
                if off + 8 + plen > len(data):
                    break
                p = data[off+8:off+8+plen]
                if t == CANP_RX:
                    for i in range(count):
                        fid, flags, dlc, r1, r2, ts = struct.unpack('<IBBBBI', p[i*20:i*20+12])
                        d = p[i*20+12:i*20+20]
                        self.rx_frames[ch].append((fid, flags, dlc, ts, d))
                elif t == CANP_TXACK:
                    for i in range(count):
                        h, status, ach = struct.unpack('<HBB', p[i*4:i*4+4])
                        self.acks[h] = (status, ach)
                elif t == CANP_CTRL_RSP:
                    self.last_ctrl_rsp = (ch, p[0], p[1], p[2:])
                off += 8 + plen


def one_direction(d, tx_ch, rx_ch, n, handle_base):
    """tx_ch发n帧, 验证rx_ch收到且内容逐帧匹配"""
    d.rx_frames[tx_ch].clear()
    d.rx_frames[rx_ch].clear()
    BATCH = 20
    sent = []
    handle = handle_base
    t0 = time.time()
    for i in range(0, n, BATCH):
        frames = []
        for j in range(BATCH):
            if i + j >= n:
                break
            fid = 0x100 + tx_ch
            data = bytes([(i + j) & 0xFF, (i + j) >> 8 & 0xFF, tx_ch, 0xA5,
                          (i + j) & 0xFF ^ 0xFF, 0x5A, 0x00, 0x00])
            frames.append((fid, 0, 8, handle, data))
            sent.append((fid, data))
            handle += 1
        d.send_frames(tx_ch, frames)
        d.pump(0.02)
    d.pump(0.5)
    dt = time.time() - t0

    got = d.rx_frames[rx_ch]
    mismatch = 0
    for i, (fid, data) in enumerate(sent):
        if i >= len(got):
            mismatch += 1
            continue
        gfid, gflags, gdlc, gts, gd = got[i]
        if gfid != fid or bytes(gd[:8]) != data:
            mismatch += 1
    extra = max(0, len(got) - len(sent))
    ack_ok = sum(1 for h in range(handle_base, handle)
                 if d.acks.get(h, (0xFF,))[0] == 0)
    print(f'CH{tx_ch}→CH{rx_ch}: 发{n} 收{len(got)} 内容错{mismatch} 多余{extra} '
          f'ACK_ok{ack_ok}/{n} 用时{dt:.1f}s')
    return mismatch == 0 and extra == 0 and len(got) == n


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
    kbps = int(sys.argv[2]) if len(sys.argv) > 2 else 1000
    d = Dev()
    print(f'设备已打开, 真实总线模式 @ {kbps}kbps')
    for ch in (0, 1):
        d.ctrl(ch, CANC_SET_BITRATE, struct.pack('<I', kbps))
        assert d.last_ctrl_rsp and d.last_ctrl_rsp[2] == 0, f'CH{ch} SET_BITRATE失败'
        d.ctrl(ch, CANC_SET_MODE, bytes([CAN_MODE_NORMAL]))
        st = d.last_ctrl_rsp[2]
        print(f'CH{ch} NORMAL模式: status={st}')
        assert st == 0, f'CH{ch} 进正常模式失败'

    ok1 = one_direction(d, 0, 1, n, 0x1000)
    ok2 = one_direction(d, 1, 0, n, 0x2000)
    print('== ' + ('全部PASS' if (ok1 and ok2) else 'FAIL') + ' ==')
    sys.exit(0 if (ok1 and ok2) else 1)


if __name__ == '__main__':
    main()
