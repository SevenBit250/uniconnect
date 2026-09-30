#!/usr/bin/env python3
"""CAN-USB桥端到端测试: GET_INFO -> 双路静默回环 -> TX帧 -> 校验RX回环+TXACK -> 对账
用法: py can_usb_test.py [总帧数每通道]"""
import sys, time, struct
import os
import usb.core, usb.util, usb.backend.libusb1

VID, PID = 0x0D28, 0x0218
IF_NUM, EP_OUT, EP_IN = 2, 0x06, 0x83   # 复合模式: CAN=接口2
DLL = r'C:\Users\25293\AppData\Local\Programs\Python\Python312\Lib\site-packages\libusb\_platform\windows\x86_64\libusb-1.0.dll'

CANP_TX, CANP_CTRL = 0x01, 0x02
CANP_RX, CANP_TXACK, CANP_EVENT, CANP_CTRL_RSP = 0x11, 0x13, 0x14, 0x15
CANC_GET_INFO, CANC_SET_BITRATE, CANC_SET_MODE, CANC_GET_STATUS = 0x01, 0x02, 0x03, 0x04

class Dev:
    def __init__(self):
        be = usb.backend.libusb1.get_backend(find_library=lambda x: DLL)
        self.dev = usb.core.find(idVendor=VID, idProduct=PID, backend=be)
        if self.dev is None:
            raise RuntimeError('设备未找到')
        self.dev.set_configuration()
        cfg = self.dev.get_active_configuration()
        self.intf = cfg[(IF_NUM, 0)]
        usb.util.claim_interface(self.dev, IF_NUM)
        self.rx_seq_seen = set()
        self.last_ctrl_rsp = None
        self.acks = {}        # handle -> status
        self.rx_frames = {0: [], 1: []}
        self.events = []

    def out(self, data):
        self.dev.write(EP_OUT, data, 1000)

    def ctrl(self, ch, cmd, payload=b''):
        # 并发: 先挂读再写(设备响应在write返回前就挂上EP3)
        import threading
        t = threading.Thread(target=self.pump, args=(0.3,))
        t.start()
        self.out(bytes([CANP_CTRL, ch, 0, 0, 1 + len(payload), 0, 0, 0]) + bytes([cmd]) + payload)
        t.join()

    def send_frames(self, ch, frames):
        """frames: [(id, flags, dlc, handle, data8)]"""
        pl = b''
        for (fid, flags, dlc, handle, data) in frames:
            pl += struct.pack('<IBBH8s', fid, flags, dlc, handle, data)
        n = len(frames)
        self.out(bytes([CANP_TX, ch, 0, 0, n & 0xFF, n >> 8, 0, 0]) + pl)

    def pump(self, duration=0.3):
        """读IN并分发, 收集数据"""
        end = time.time() + duration
        while time.time() < end:
            try:
                data = self.dev.read(EP_IN, 512, 50)
            except usb.core.USBError:
                continue
            off = 0
            while off + 8 <= len(data):
                t, ch = data[off], data[off+1]
                seq = data[off+2] | (data[off+3] << 8)
                count = data[off+4] | (data[off+5] << 8)
                if t == CANP_RX:
                    plen = count * 20
                elif t == CANP_TXACK:
                    plen = count * 4
                elif t == CANP_EVENT:
                    plen = count * 8
                elif t == CANP_CTRL_RSP:
                    plen = count   # 固件: count=应答payload总长(含cmd)
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
                elif t == CANP_EVENT:
                    for i in range(count):
                        ev = p[i*8]
                        v = struct.unpack('<I', p[i*8+2:i*8+6])[0]
                        self.events.append((ev, v))
                elif t == CANP_CTRL_RSP:
                    self.last_ctrl_rsp = (ch, p[0], p[1], p[2:])
                self.rx_seq_seen.add(seq)
                off += 8 + plen

def main():
    nframes = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
    d = Dev()
    print('设备已打开, 接口2端点 0x06/0x83')

    for _ in range(10):
        d.ctrl(0, CANC_GET_INFO)
        d.pump(0.2)
        if d.last_ctrl_rsp:
            break
    ch_, cmd, st, pl = d.last_ctrl_rsp
    print(f'GET_INFO: proto={pl[0]} ch_num={pl[1]} status={st}')
    assert pl[0] == 1 and pl[1] == 2 and st == 0, 'GET_INFO异常'

    for ch in (0, 1):
        d.ctrl(ch, CANC_SET_MODE, bytes([0]))   # 静默回环
        d.pump(0.1)
        print(f'CH{ch} SET_MODE(loopback): status={d.last_ctrl_rsp[2]}')
        assert d.last_ctrl_rsp[2] == 0

    # 取设备计数基数
    base = {0: (0, 0), 1: (0, 0)}
    for ch in (0, 1):
        d.ctrl(ch, CANC_GET_STATUS)
        d.pump(0.2)
        _, cmd, st, pl = d.last_ctrl_rsp
        tx, rx, err, kbps, mode = struct.unpack('<IIIII', pl[:20])
        base[ch] = (tx, rx)

    # 双路发帧: id=0x100+ch, data=handle序列
    t0 = time.time()
    sent = {0: [], 1: []}
    BATCH = 20
    sent_ack = 0
    for i in range(0, nframes, BATCH):
        # ACK节流: 等上一批回执到齐再灌下一批(CAN清环2.6ms/20帧)
        t_end = time.time() + 0.5
        while time.time() < t_end and len(d.acks) < sent_ack + 2 * min(BATCH, nframes - i):
            d.pump(0.02)
        sent_ack = len(d.acks)
        for ch in (0, 1):
            frames = []
            for j in range(min(BATCH, nframes - i)):
                h = i + j + 1
                data = struct.pack('<I', h) + struct.pack('<I', (h * 2654435761) & 0xFFFFFFFF)
                frames.append((0x100 + ch, 0, 8, h, data))
                sent[ch].append(h)
            d.send_frames(ch, frames)
        d.pump(0.02)
    # 收尾读: 收满或超时
    deadline = time.time() + 6
    def got_h(ch):
        return len(d.rx_frames[ch])
    while time.time() < deadline and (got_h(0) < nframes or got_h(1) < nframes or len(d.acks) < 2 * nframes):
        d.pump(0.3)
    dt = time.time() - t0

    ok = True
    for ch in (0, 1):
        rxf = d.rx_frames[ch]
        exp = sent[ch]
        got_h = [struct.unpack('<I', f[4][0:4])[0] for f in rxf]
        # 回环内容校验: 每帧data[0:4]=handle, data[4:8]=hash(handle)
        bad = 0
        for f in rxf:
            h = struct.unpack('<I', f[4][0:4])[0]
            if f[0] != 0x100 + ch or f[1] != 0 or f[2] != 8:
                bad += 1
            elif struct.unpack('<I', f[4][4:8])[0] != (h * 2654435761) & 0xFFFFFFFF:
                bad += 1
        missing = sorted(set(exp) - set(got_h))
        extra = sorted(set(got_h) - set(exp))
        ts_bad = sum(1 for f in rxf if f[3] == 0)
        # ACK覆盖
        unacked = [h for h in exp if d.acks.get(h) is None]
        nak = [h for h in exp if d.acks.get(h, (9, 9))[0] != 0]
        print(f'CH{ch}: 发{len(exp)} 回环{len(rxf)} 缺失{len(missing)} 多余{len(extra)} '
              f'内容错{bad} ts=0({ts_bad}) ACK缺{len(unacked)} ACK非SENT{len(nak)}')
        if missing[:5]: print(f'   缺失样例{missing[:5]}')
        if extra[:5]: print(f'   多余样例{extra[:5]}')
        if bad or missing or extra or ts_bad or unacked or nak:
            ok = False

    # seq连续性(设备->主机包序号)
    seqs = sorted(d.rx_seq_seen)
    gaps = [b for a, b in zip(seqs, seqs[1:]) if b != a + 1]
    print(f'设备包seq: {len(seqs)}个, 间隙{len(gaps)}处' + (f' @首处{gaps[0]}' if gaps else ''))
    if gaps:
        ok = False

    # 设备计数器对账(累计值, 比对本轮增量)
    for ch in (0, 1):
        d.ctrl(ch, CANC_GET_STATUS)
        d.pump(0.2)
        _, cmd, st, pl = d.last_ctrl_rsp
        tx, rx, err, kbps, mode = struct.unpack('<IIIII', pl[:20])
        dtx, drx = tx - base[ch][0], rx - base[ch][1]
        print(f'CH{ch} 设备计数增量: tx={dtx} rx={drx} err={err} bitrate={kbps} mode={mode}')
        if drx != len(d.rx_frames[ch]):
            print(f'   ! 设备rx增量({drx}) != 主机收到({len(d.rx_frames[ch])})')
            ok = False

    print(f'用时{dt:.1f}s 双路合计{2*nframes}帧')
    print('==', '全部PASS' if ok else 'FAIL', '==')
    sys.exit(0 if ok else 1)

if __name__ == '__main__':
    main()
