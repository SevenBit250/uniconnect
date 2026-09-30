#!/usr/bin/env python3
"""uniconnect CDC 回环测试(板上 UART TX-RX 已短接)

覆盖路径:
  1. IDLE部分块(小包)   2. 整块边界(512/ZLP)   3. 多块大流量完整性
  4. 波特率切换(双向池重建)   5. 吞吐率

用法: py tools/cdc_loopback_test.py
"""
import sys
import time
import random
import threading

import serial
import serial.tools.list_ports

VID, PID = 0x0D28, 0x0218


def find_port():
    for p in serial.tools.list_ports.comports():
        if p.vid == VID and p.pid == PID:
            return p.device
    return None


def pattern(n, seed=0):
    """确定性LCG伪随机序列, 失败时可复现"""
    x = (seed * 2654435761 + 1) & 0xFFFFFFFF or 1
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


def drain(ser, quiet=0.3):
    """读空缓冲, 静默quiet秒无数据认为空闲"""
    end = time.time() + quiet
    while time.time() < end:
        if ser.read(4096):
            end = time.time() + quiet


def loopback(ser, data, timeout, label, concurrent=False, pace=0):
    ser.reset_input_buffer()
    t0 = time.time()
    if concurrent:
        # 分块写+节奏: 单笔大写会让usbser写路径pending而饿死读(主机驱动行为),
        # 节奏防止写队列堆深; 高波特率不需要节奏(会制造IDLE部分块风暴)
        def w():
            for i in range(0, len(data), 4096):
                ser.write(data[i:i+4096])
                ser.flush()
                if pace:
                    time.sleep(pace)
        t = threading.Thread(target=w)
        t.start()
        got = read_exact(ser, len(data), timeout)
        t.join(15)
    else:
        ser.write(data)
        ser.flush()
        got = read_exact(ser, len(data), timeout)
    dt = time.time() - t0
    ok = got == data
    suffix = '' if ok else ' (数据不一致!)'
    print(f"  [{label}] {len(data)}B -> {'PASS' if ok else 'FAIL'}  {dt:.2f}s{suffix}")
    if not ok:
        if len(got) < len(data):
            print(f"    不足: 收到{len(got)}/{len(data)}字节(超时)")
        for i in range(min(len(got), len(data))):
            if got[i] != data[i]:
                print(f"    首个差异@{i}: 期望{data[i]:02X} 实收{got[i]:02X}")
                break
    return ok


def main():
    port = find_port()
    if not port:
        print(f"未找到设备 vid={VID:04x} pid={PID:04x}")
        return 1
    print(f"设备: {port}")

    results = []
    ser = serial.Serial(port, 115200, timeout=0.2, write_timeout=60)
    time.sleep(1.0)  # 等线控握手(ctrl线程含LED闪烁)
    drain(ser)

    print("== 1. 小包/IDLE路径 @115200 ==")
    results.append(loopback(ser, b"A", 2, "单字节"))
    results.append(loopback(ser, b"hello uniconnect!", 2, "17字节"))
    random.seed(42)
    data = bytearray()
    for i in range(100):
        data += pattern(random.randint(1, 64), i)
    results.append(loopback(ser, bytes(data), 5, "100x随机小包(1-64B)"))

    print("== 2. 整块边界 @115200 ==")
    results.append(loopback(ser, pattern(511), 2, "511B(块边界-1)"))
    results.append(loopback(ser, pattern(512), 2, "512B(整块+ZLP)"))
    results.append(loopback(ser, pattern(513), 2, "513B(块边界+1)"))
    results.append(loopback(ser, pattern(1024), 2, "1024B(两整块)"))

    print("== 3. 大流量完整性 @115200 (并发读写+10ms节奏: 单笔大写会饿死主机读, 属usbser驱动行为) ==")
    results.append(loopback(ser, pattern(64 * 1024, 7), 30, "64KB并发", concurrent=True, pace=0.01))

    print("== 4. 波特率切换(池重建路径) ==")
    for baud, tag in ((921600, "921600"), (115200, "回115200")):
        ser.baudrate = baud
        time.sleep(1.0)
        drain(ser)
        results.append(loopback(ser, pattern(4096, baud & 0xFF), 10, f"切{tag}后4096B"))

    print("== 5. 吞吐率 @921600 (并发) ==")
    ser.baudrate = 921600
    time.sleep(1.0)
    drain(ser)
    n = 256 * 1024
    data = pattern(n, 99)
    ser.reset_input_buffer()
    t0 = time.time()
    def w5():
        for i in range(0, n, 4096):
            ser.write(data[i:i+4096])
            ser.flush()
    t = threading.Thread(target=w5)
    t.start()
    got = read_exact(ser, n, 90)
    t.join(15)
    dt = time.time() - t0
    ok = got == data
    results.append(ok)
    print(f"  [256KB@921600并发] {'PASS' if ok else 'FAIL'}  往返{dt:.2f}s 单向有效{n/dt/1024:.1f}KB/s 收到{len(got)}")

    ser.close()
    npass = sum(1 for r in results if r)
    print(f"\n结果: {npass}/{len(results)} PASS")
    return 0 if npass == len(results) else 2


if __name__ == "__main__":
    sys.exit(main())
