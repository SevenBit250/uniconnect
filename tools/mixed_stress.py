#!/usr/bin/env python3
"""mixed_stress.py -- CAN+UART(CDC)真并行混合压测(多进程版)

主机侧约束(2026-10-01实锚): Windows上pyserial先开COM口后, libusb对同一
USB复合设备open被拒(Access denied); 反序可共存。故CAN进程先启动(libusb
claim接口3), 2秒后CDC进程启动(pyserial开COM10), 之后两进程全时长并发。

CAN进程: 真实总线双通道对接, 双向持续轮转发帧+逐帧内容校验。
  CH1延迟CH2_OPEN_DELAY_S打开(灯语观察: 单通道常亮期), 结束后SET_MODE(SLEEP)
  关闭双通道(CAN灯灭, 硬件回close)。
CDC进程: 满速写+并发读线程(物理回环), 累计字节数。

用法: python tools/mixed_stress.py [分钟] [CAN波特率] [CDC波特率]
"""
import sys
import time
import struct
import multiprocessing as mp

CAN_KBPS_DEFAULT = 1000
CDC_BAUD_DEFAULT = 5000000
PORT = 'COM10'


def can_proc(dur_s, kbps, q):
    import usb.core
    import usb.util
    import usb.backend.libusb1
    sys.path.insert(0, 'tools')
    DLL = (r'C:\Users\25293\AppData\Local\Programs\Python\Python312\Lib'
           r'\site-packages\libusb\_platform\windows\x86_64\libusb-1.0.dll')
    be = usb.backend.libusb1.get_backend(find_library=lambda x: DLL)

    VID, PID = 0x0D28, 0x0218
    IF_NUM, EP_OUT, EP_IN = 3, 0x06, 0x83
    CANP_TX, CANP_CTRL, CANP_RX, CANP_TXACK, CANP_EVENT, CANP_CTRL_RSP = \
        0x01, 0x02, 0x11, 0x13, 0x14, 0x15
    CANC_SET_BITRATE, CANC_SET_MODE = 0x02, 0x03
    CAN_MODE_NORMAL, CAN_MODE_SLEEP = 1, 2
    CH2_OPEN_DELAY_S = 3.0   # CH1延迟打开: 制造两通道open错峰(灯语观察)

    import threading

    class D:
        pass

    d = D()
    d.dev = usb.core.find(idVendor=VID, idProduct=PID, backend=be)
    if d.dev is None:
        q.put(('can_err', '设备未找到')); return
    cfg = d.dev.get_active_configuration()
    usb.util.claim_interface(d.dev, IF_NUM)
    d.rx_frames = {0: [], 1: []}
    d.acks = {}
    d.last_ctrl_rsp = None

    def out(data):
        d.dev.write(EP_OUT, data, 1000)

    def ctrl(ch, cmd, payload=b''):
        t = threading.Thread(target=pump, args=(0.3,))
        t.start()
        out(bytes([CANP_CTRL, ch, 0, 0, 1 + len(payload), 0, 0, 0])
            + bytes([cmd]) + payload)
        t.join()

    def send_frames(ch, frames):
        pl = b''
        for (fid, flags, dlc, handle, data) in frames:
            pl += struct.pack('<IBBH8s', fid, flags, dlc, handle, data)
        n = len(frames)
        out(bytes([CANP_TX, ch, 0, 0, n & 0xFF, n >> 8, 0, 0]) + pl)

    def pump(duration=0.3):
        end = time.time() + duration
        while time.time() < end:
            try:
                data = d.dev.read(EP_IN, 512, 50)
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
                        fid, flags, dlc, r1, r2, ts = struct.unpack(
                            '<IBBBBI', p[i*20:i*20+12])
                        dd = p[i*20+12:i*20+20]
                        d.rx_frames[ch].append((fid, flags, dlc, ts, dd))
                elif t == CANP_TXACK:
                    for i in range(count):
                        h, status, ach = struct.unpack('<HBB', p[i*4:i*4+4])
                        d.acks[h] = (status, ach)
                elif t == CANP_CTRL_RSP:
                    d.last_ctrl_rsp = (ch, p[0], p[1], p[2:])
                off += 8 + plen

    ctrl(0, CANC_SET_BITRATE, struct.pack('<I', kbps))
    ctrl(0, CANC_SET_MODE, bytes([CAN_MODE_NORMAL]))
    time.sleep(CH2_OPEN_DELAY_S)
    ctrl(1, CANC_SET_BITRATE, struct.pack('<I', kbps))
    ctrl(1, CANC_SET_MODE, bytes([CAN_MODE_NORMAL]))

    def one_dir(tx_ch, rx_ch, n, h0):
        d.rx_frames[tx_ch].clear()
        d.rx_frames[rx_ch].clear()
        sent = []
        h = h0
        BATCH = 20
        for i in range(0, n, BATCH):
            frames = []
            for j in range(BATCH):
                if i + j >= n:
                    break
                fid = 0x100 + tx_ch
                data = bytes([(i+j) & 0xFF, (i+j) >> 8 & 0xFF, tx_ch, 0xA5,
                              (i+j) & 0xFF ^ 0xFF, 0x5A, 0, 0])
                frames.append((fid, 0, 8, h, data))
                sent.append((fid, data))
                h += 1
            send_frames(tx_ch, frames)
            pump(0.02)
        pump(0.4)
        got = d.rx_frames[rx_ch]
        mm = sum(1 for i, (fid, data) in enumerate(sent)
                 if i >= len(got) or got[i][0] != fid or bytes(got[i][4][:8]) != data)
        extra = max(0, len(got) - len(sent))
        return len(got) == n and mm == 0 and extra == 0

    rounds = fails = 0
    end = time.time() + dur_s
    h = 0x5000
    while time.time() < end:
        rounds += 1
        ok = one_dir(0, 1, 200, h & 0xFFFF) and one_dir(1, 0, 200, (h + 0x100) & 0xFFFF)
        h = (h + 0x1000) & 0xFFFF
        if not ok:
            fails += 1

    for ch in (0, 1):   # 结束关闭双通道(硬件回close, CAN灯灭)
        ctrl(ch, CANC_SET_MODE, bytes([CAN_MODE_SLEEP]))
    q.put(('can', rounds, fails))


def cdc_proc(dur_s, baud, q):
    import serial
    import threading
    s = serial.Serial(PORT, 115200, timeout=0.2, write_timeout=30)
    time.sleep(1.0)
    s.baudrate = baud
    time.sleep(0.8)
    s.reset_input_buffer()
    running = [True]
    total = [0]

    def reader():
        while running[0]:
            data = s.read(4096)
            if data:
                total[0] += len(data)

    rt = threading.Thread(target=reader)
    rt.start()
    chunk = b'\x55' * 512
    sent = 0
    end = time.time() + dur_s
    while time.time() < end:
        for _ in range(8):
            s.write(chunk)
            sent += len(chunk)
            time.sleep(0.001)
    running[0] = False
    rt.join(timeout=2)
    time.sleep(0.5)
    q.put(('cdc', sent, total[0]))
    s.close()


def main():
    minutes = int(sys.argv[1]) if len(sys.argv) > 1 else 30
    can_kbps = int(sys.argv[2]) if len(sys.argv) > 2 else CAN_KBPS_DEFAULT
    cdc_baud = int(sys.argv[3]) if len(sys.argv) > 3 else CDC_BAUD_DEFAULT
    dur = minutes * 60
    print(f'真并行混合压测 {minutes}分钟: CAN@{can_kbps}kbps + CDC@{cdc_baud} '
          f'(双进程, CAN先开)', flush=True)

    q = mp.Queue()
    pcan = mp.Process(target=can_proc, args=(dur, can_kbps, q))
    pcan.start()
    time.sleep(2)          # libusb claim完成后再开pyserial(顺序敏感)
    pcdc = mp.Process(target=cdc_proc, args=(dur, cdc_baud, q))
    pcdc.start()
    pcan.join()
    pcdc.join()

    res = {}
    while not q.empty():
        r = q.get()
        res[r[0]] = r
    print('----- 结果 -----')
    if 'can' in res:
        _, rounds, fails = res['can']
        print(f'CAN: {rounds}轮(每轮400帧)={rounds*400}帧, 失败轮数={fails}')
    if 'cdc' in res:
        _, sent, recv = res['cdc']
        print(f'CDC: 发{sent/1e6:.1f}MB 收{recv/1e6:.1f}MB ({recv/max(1,sent)*100:.2f}%)')
    ok = (res.get('can', (0, 0, 1))[2] == 0 and
          res.get('cdc', (0, 0, 0))[2] >= res.get('cdc', (0, 0, 1))[1] * 0.999)
    print('== ' + ('全部PASS' if ok else 'FAIL') + ' ==')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    mp.freeze_support()
    main()
