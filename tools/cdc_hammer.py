#!/usr/bin/env python3
"""持续满载长锤: 指定波特率/单轮字节数/轮数, 全速并发分块回环, 字节级校验
用法: py cdc_hammer.py COM5 4000000 262144 5   # 4M, 256KB/轮, 5轮"""
import sys, time, threading
import serial

def pattern(n, seed=0):
    x = ((seed*2654435761+1)&0xFFFFFFFF) or 1
    out = bytearray(n)
    for i in range(n):
        x = (x*1103515245+12345)&0xFFFFFFFF
        out[i] = (x>>16)&0xFF
    return bytes(out)

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM5"
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 4000000
    size = int(sys.argv[3]) if len(sys.argv) > 3 else 262144
    rounds = int(sys.argv[4]) if len(sys.argv) > 4 else 5
    ser = serial.Serial(port, 115200, timeout=0.2, write_timeout=30)
    time.sleep(1.0)
    ser.baudrate = baud
    time.sleep(0.8)
    ser.reset_input_buffer()
    ser.read(65536)
    ser.reset_input_buffer()
    total_fail = 0
    for r in range(rounds):
        d = pattern(size, (baud & 0xFF) + r)
        got = bytearray()
        t0 = time.time()
        def w():
            try:
                for i in range(0, size, 4096):
                    ser.write(d[i:i+4096])
                    ser.flush()
            except Exception:
                pass
        t = threading.Thread(target=w)
        t.start()
        end = t0 + size/(baud/10) + 10
        while len(got) < size and time.time() < end:
            got += ser.read(16384)
        t.join(5)
        dt = time.time() - t0
        ok = bytes(got) == d
        if not ok:
            total_fail += 1
            # 定位首个失配位置
            mm = next((i for i in range(min(len(got), size)) if got[i] != d[i]), -1)
            print(f"  轮{r+1}: FAIL 收到{len(got)}/{size} 首失配@{mm} "
                  f"(块{mm//512 if mm>=0 else '-'})")
        else:
            thr = size/dt/1024
            print(f"  轮{r+1}: PASS {dt:.2f}s {thr:.0f}KB/s")
        ser.reset_input_buffer()
    ser.close()
    print(f"== {baud} {size}B×{rounds}: {'全部PASS' if total_fail==0 else f'{total_fail}轮FAIL'} ==")
    sys.exit(1 if total_fail else 0)

if __name__ == "__main__":
    main()
