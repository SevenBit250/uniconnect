#!/usr/bin/env python3
"""混合阶段专项复现+取证: 等差/sin/随机轮转, 失败时分析缺失切片结构
用法: py cdc_mixed_focus.py [PORT] [BAUD] [SECONDS]"""
import sys, time, hashlib, threading, math, random
import serial

def pattern(n, seed=0):
    x = ((seed*2654435761+1)&0xFFFFFFFF) or 1
    out = bytearray(n)
    for i in range(n):
        x = (x*1103515245+12345)&0xFFFFFFFF
        out[i] = (x>>16)&0xFF
    return bytes(out)

def forensics(p, got, tag, bn, size):
    """分析got相对p缺失的切片结构"""
    if len(got) == 0:
        print(f"  [{tag}]批{bn} size={size} 收到0B", flush=True)
        return
    d = next((i for i in range(min(len(got), len(p))) if got[i] != p[i]), -1)
    if d < 0:
        print(f"  [{tag}]批{bn} size={size} 收到{len(got)}B 前缀完全一致(短读{len(p)-len(got)}B)", flush=True)
        return
    # got[d:d+64] 在p中重新定位 → 缺失长度与位置
    probe = bytes(got[d:d+64])
    pos = p.find(probe)
    loss = pos - d if pos >= 0 else -1
    print(f"  [{tag}]批{bn} size={size} 收到{len(got)}B 首异@{d} (块{d//512})"
          f" got续写在p中@{pos} → 缺失{loss}B @{d}", flush=True)
    if loss > 0 and (d % 512 == 0) and (loss % 512 == 0):
        print(f"    * 整块缺失特征: {loss//512}块x512B @块{d//512}", flush=True)
    print(f"    p[{max(0,d-4)}:{d+12}]  = {p[max(0,d-4):d+12].hex()}", flush=True)
    print(f"    got[{max(0,d-4)}:{d+12}] = {bytes(got[max(0,d-4):d+12]).hex()}", flush=True)

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM5"
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 5000000
    dur  = int(sys.argv[3]) if len(sys.argv) > 3 else 300
    base = [pattern(4*1024*1024, 0x5A0000+i) for i in range(4)]
    md5c = {}
    ser = serial.Serial(port, 115200, timeout=0.2, write_timeout=30)
    time.sleep(1.0)
    ser.baudrate = baud
    time.sleep(0.8)
    ser.reset_input_buffer(); ser.read(65536); ser.reset_input_buffer()

    st = {"ap": 0}
    t0 = time.time()
    def g_ap():
        st["ap"] = 1 if st["ap"] >= 65536 else st["ap"] + 1
        return st["ap"]
    def g_sin():
        t = time.time() - t0
        return 1 + int(511*1024 * (0.5 + 0.5*math.sin(2*math.pi*t/60)))
    def g_rnd():
        return max(1, min(1000000, int(10 ** random.uniform(0, 6))))

    batches = passes = fails = 0
    tx = rx = 0
    t_end = time.time() + dur
    vi = 0
    next_prog = time.time() + 60
    while time.time() < t_end:
        m = batches % 3
        size = int((g_ap, g_sin, g_rnd)[m]())
        p = base[vi & 3][:size]
        got = bytearray()
        tb = time.time()
        if size >= 16384:
            def w():
                try:
                    for i in range(0, size, 4096):
                        ser.write(p[i:i+4096]); ser.flush()
                except Exception:
                    pass
            th = threading.Thread(target=w); th.start()
        else:
            try:
                ser.write(p); ser.flush()
            except Exception:
                pass
        deadline = tb + max(2*size*10/baud + 15, 5)
        while len(got) < size and time.time() < deadline:
            got += ser.read(min(16384, size - len(got)))
        if size >= 16384:
            th.join(10)
        batches += 1; tx += size; rx += len(got)
        key = (vi & 3, size)
        h = md5c.get(key)
        if h is None:
            h = hashlib.md5(p).hexdigest(); md5c[key] = h
        if len(got) == size and hashlib.md5(bytes(got)).hexdigest() == h:
            passes += 1
        else:
            fails += 1
            forensics(p, got, "混合", batches, size)
            ser.reset_input_buffer()
        vi += 1
        if time.time() >= next_prog:
            next_prog += 60
            print(f"[{time.time()-t0:5.0f}s] 批{batches} 过{passes} 败{fails} TX{tx/1048576:.1f}MB", flush=True)
    ser.close()
    print(f"== 混合专项: {batches}批 过{passes} 败{fails} TX {tx}B RX {rx}B ==", flush=True)
    sys.exit(0 if fails == 0 else 1)

if __name__ == "__main__":
    main()
