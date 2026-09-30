#!/usr/bin/env python3
"""波特率上限扫描: 各档位64KB并发分块回环, 记录正确性与吞吐"""
import sys, time, threading
import serial

PORT = "COM5"

def pattern(n, seed=0):
    x = ((seed*2654435761+1)&0xFFFFFFFF) or 1
    out = bytearray(n)
    for i in range(n):
        x = (x*1103515245+12345)&0xFFFFFFFF
        out[i] = (x>>16)&0xFF
    return bytes(out)

def read_exact(ser, n, timeout):
    buf = bytearray()
    end = time.time() + timeout
    while len(buf) < n and time.time() < end:
        chunk = ser.read(n - len(buf))
        if chunk:
            buf += chunk
    return bytes(buf)

def test_baud(ser, baud, n=64*1024):
    try:
        ser.baudrate = baud
    except Exception as e:
        return ('SETFAIL', 0, str(e))
    time.sleep(0.8)
    ser.reset_input_buffer()
    ser.read(65536)
    d = pattern(n, baud & 0xFF)
    ser.reset_input_buffer()
    got = bytearray()
    t0 = time.time()
    def w():
        try:
            pace = 0.01 if baud <= 400000 else 0
            for i in range(0, n, 4096):
                ser.write(d[i:i+4096])
                ser.flush()
                if pace:
                    time.sleep(pace)
        except Exception:
            pass
    t = threading.Thread(target=w)
    t.start()
    end = t0 + n/ (baud/10) + 8   # 线速时间 + 8s余量
    while len(got) < n and time.time() < end:
        got += ser.read(16384)
    t.join(5)
    dt = time.time() - t0
    ok = bytes(got) == d
    return ('PASS' if ok else 'FAIL', dt, len(got))

def main():
    bauds = [921600, 1000000, 1500000, 2000000, 3000000, 4000000, 5000000, 6000000]
    ser = serial.Serial(PORT, 115200, timeout=0.2, write_timeout=20)
    time.sleep(1.0)
    print(f"{'波特率':>9} | {'结果':>7} | {'用时':>7} | 吞吐(KB/s) | 收到B")
    print('-' * 56)
    for b in bauds:
        st, dt, n = test_baud(ser, b)
        if st == 'SETFAIL':
            print(f"{b:>9} | 设置失败: {n}")
            continue
        thr = (n/dt/1024) if dt > 0 else 0
        wire = b/10/1024
        print(f"{b:>9} | {st:>7} | {dt:>6.1f}s | {thr:>9.1f} | {n}  (线速上限{wire:.0f})")
    ser.close()

if __name__ == "__main__":
    sys.exit(main())
