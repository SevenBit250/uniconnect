#!/usr/bin/env python3
"""64KB故障定位实验v2: 并发读写 / 移位分析 / 固件计数器快照(魔法波特率1234567)"""
import sys, time, threading, struct
import serial

PORT = "COM5"
DBG_MAGIC_BAUD = 1234567

FIELDS = ["uart_idle_evt", "uart_tc_evt", "uart_idle_zero", "uart_submits",
          "uart_pause", "uart_resume_ok", "uart_resume_fail",
          "out_done", "out_zlp", "out_pause", "out_resume_ok", "out_resume_fail",
          "in_pop", "in_recycle", "in_zlp", "in_write_err", "in_write_ret",
          "tx_dma", "tx_dma_zero", "recycle_drop", "submit_drop"]

def pattern(n, seed=0):
    x = ((seed * 2654435761 + 1) & 0xFFFFFFFF) or 1
    out = bytearray(n)
    for i in range(n):
        x = (x * 1103515245 + 12345) & 0xFFFFFFFF
        out[i] = (x >> 16) & 0xFF
    return bytes(out)

def read_exact(ser, n, timeout):
    buf = bytearray()
    end = time.time() + timeout
    while len(buf) < n and time.time() < end:
        chunk = ser.read(n - len(buf))
        if chunk:
            buf += chunk
    return bytes(buf)

def read_counters(ser, tag):
    """设魔法波特率读回固件计数器快照(走CDC IN, 不经过UART)"""
    try:
        ser.reset_input_buffer()
        ser.baudrate = DBG_MAGIC_BAUD   # 与当前值不同才触发, 固件按memcmp去重
        time.sleep(0.5)
        raw = read_exact(ser, len(FIELDS) * 4, 3.0)
        if len(raw) < len(FIELDS) * 4:
            print(f"  [计数器@{tag}] 读取不完整: {len(raw)}B")
            return None
        vals = struct.unpack("<" + "I" * len(FIELDS), raw)
        d = dict(zip(FIELDS, vals))
        print(f"  [计数器@{tag}] " + " ".join(f"{k}={v}" for k, v in d.items()))
        return d
    except Exception as e:
        print(f"  [计数器@{tag}] 读取失败: {e}")
        return None

def analyze(rx, tx):
    diff = None
    for i in range(min(len(rx), len(tx))):
        if rx[i] != tx[i]:
            diff = i
            break
    print(f"  首个差异: @{diff} (rx={len(rx)}, tx={len(tx)})")
    if diff is None:
        return
    needle = rx[diff:diff+64]
    fwd = tx.find(needle, diff)
    back = tx.rfind(needle, 0, diff)
    fwd_s = f"{fwd}(丢{fwd-diff}B)" if fwd >= 0 else "无"
    back_s = f"{back}(重复{diff-back}B)" if back >= 0 else "无"
    print(f"  错乱区匹配: 作为tx后移版本={fwd_s} 作为tx前移版本={back_s}")
    if len(rx) > diff + 256:
        pos = tx.find(rx[-256:])
        print(f"  rx尾部256B在tx中的偏移: {pos} (若无损应={len(rx)-256})")

def run(name, ser, n, seed, concurrent, timeout=60):
    data = pattern(n, seed)
    ser.reset_input_buffer()
    t0 = time.time()
    if concurrent:
        def writer():
            ser.write(data)
            ser.flush()
        t = threading.Thread(target=writer)
        t.start()
        got = read_exact(ser, n, timeout)
        t.join(10)
    else:
        ser.write(data)
        ser.flush()
        got = read_exact(ser, n, timeout)
    dt = time.time() - t0
    ok = got == data
    print(f"[{name}] {n}B 并发={concurrent} -> {'PASS' if ok else 'FAIL'} {dt:.1f}s 收到{len(got)}")
    if not ok:
        analyze(bytes(got), data)
    return ok

def main():
    ser = serial.Serial(PORT, 115200, timeout=0.1, write_timeout=60)
    time.sleep(1.0)
    ser.reset_input_buffer()
    ser.read(65536)

    read_counters(ser, "基线")
    ser.baudrate = 115200; time.sleep(0.8); ser.reset_input_buffer(); ser.read(65536)

    print("== B. 64KB 串行(旧版必挂用例) ==")
    ok = run("64KB串行", ser, 64*1024, 11, False, 40)
    read_counters(ser, "64KB串行后")

    print("== A. 64KB 并发读写 ==")
    try:
        ser.baudrate = 115200
    except Exception as e:
        print("  (恢复波特率失败, 重开端口)", e)
        ser.close()
        ser = serial.Serial(PORT, 115200, timeout=0.1, write_timeout=60)
        time.sleep(1.0)
    time.sleep(0.8); ser.reset_input_buffer(); ser.read(65536)
    ok2 = run("64KB并发", ser, 64*1024, 22, True, 60)
    read_counters(ser, "64KB并发后")
    ser.close()

if __name__ == "__main__":
    sys.exit(main())
