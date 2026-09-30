#!/usr/bin/env python3
"""对照实验: 快照计数器 -> 混合负载 -> 快照, 四点字节账目+ORE对账
用法: py cdc_delta_exp.py [PORT] [BAUD] [SECONDS]"""
import sys, time, hashlib, threading, math, random, struct
import serial

DBG_MAGIC = 1234567
NFIELDS  = 29   # 25原有 + rx_ore/rx_fe/rx_ne/rx_defer
NAMES = ["uart_idle_evt","uart_tc_evt","uart_idle_zero","uart_submits",
         "uart_pause","uart_resume_ok","uart_resume_fail",
         "out_done","out_zlp","out_pause","out_resume_ok","out_resume_fail",
         "in_pop","in_recycle","in_bytes","out_bytes","tx_bytes",
         "in_zlp","in_write_err","in_write_ret","in_write_ptr",
         "tx_dma","tx_dma_zero","recycle_drop","submit_drop",
         "rx_ore","rx_fe","rx_ne","rx_defer"]

def pattern(n, seed=0):
    x = ((seed*2654435761+1)&0xFFFFFFFF) or 1
    out = bytearray(n)
    for i in range(n):
        x = (x*1103515245+12345)&0xFFFFFFFF
        out[i] = (x>>16)&0xFF
    return bytes(out)

def snapshot(ser):
    ser.baudrate = DBG_MAGIC
    time.sleep(0.5)
    raw = bytearray()
    end = time.time() + 3
    need = NFIELDS * 4
    while len(raw) < need and time.time() < end:
        c = ser.read(need - len(raw))
        if c:
            raw += c
    if len(raw) < need:
        return None
    return dict(zip(NAMES, struct.unpack("<" + "I"*NFIELDS, bytes(raw))))

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM5"
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 5000000
    dur  = int(sys.argv[3]) if len(sys.argv) > 3 else 300
    base = [pattern(4*1024*1024, 0x5A0000+i) for i in range(4)]
    ser = serial.Serial(port, 115200, timeout=0.2, write_timeout=30)
    time.sleep(1.0)
    c0 = snapshot(ser)
    ser.baudrate = baud
    time.sleep(0.8)
    ser.reset_input_buffer(); ser.read(65536); ser.reset_input_buffer()

    st = {"ap": 0}
    t0 = time.time()
    def g_ap():
        st["ap"] = 1 if st["ap"] >= 65536 else st["ap"] + 1
        return st["ap"]
    def g_sin():
        return 1 + int(511*1024 * (0.5 + 0.5*math.sin(2*math.pi*(time.time()-t0)/60)))
    def g_rnd():
        return max(1, min(1000000, int(10 ** random.uniform(0, 6))))

    batches = fails = 0
    tx = rx = 0
    lost_total = 0
    t_end = time.time() + dur
    vi = 0
    md5c = {}
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
            pass
        else:
            fails += 1
            lost_total += size - len(got)
            ser.reset_input_buffer()
        vi += 1
    c1 = snapshot(ser)
    ser.close()

    print(f"批次{batches} 失败{fails} 主机TX {tx}B RX {rx}B 缺口{tx-rx}B", flush=True)
    if c0 and c1:
        d = {k: c1[k]-c0[k] for k in NAMES}
        print("== 增量 ==")
        for k in NAMES:
            if d[k]:
                print(f"  {k:16s} +{d[k]}")
        print("== 对账 ==")
        print(f"  主机TX        = {tx}")
        print(f"  out_bytes增量 = {d['out_bytes']}  (差={d['out_bytes']-tx}, USB OUT侧丢)")
        print(f"  tx_bytes增量  = {d['tx_bytes']}  (差={d['tx_bytes']-d['out_bytes']}, OUT->UART TX丢)")
        print(f"  in_bytes增量  = {d['in_bytes']}  (vs 主机RX={rx}, 差={d['in_bytes']-rx}, IN侧/主机丢)")
        print(f"  线级丢字节缺口= {tx-rx}")
        print(f"  rx_ore增量    = {d['rx_ore']}  rx_fe=+{d['rx_fe']} rx_ne=+{d['rx_ne']}")
        print(f"  submit_drop   = +{d['submit_drop']} recycle_drop=+{d['recycle_drop']}")
    sys.exit(0 if fails == 0 else 1)

if __name__ == "__main__":
    main()
