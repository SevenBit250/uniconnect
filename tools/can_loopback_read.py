#!/usr/bin/env python3
"""CAN双路回环自测读数: 两次快照取增量, 验证 tx==rx 且 err/drop==0
用法: py can_loopback_read.py [PORT] [间隔秒]"""
import sys, time, struct
import serial

DBG_MAGIC = 1234567
NFIELDS  = 37
NAMES = ["uart_idle_evt","uart_tc_evt","uart_idle_zero","uart_submits",
         "uart_pause","uart_resume_ok","uart_resume_fail",
         "out_done","out_zlp","out_pause","out_resume_ok","out_resume_fail",
         "in_pop","in_recycle","in_bytes","out_bytes","tx_bytes",
         "in_zlp","in_write_err","in_write_ret","in_write_ptr",
         "tx_dma","tx_dma_zero","recycle_drop","submit_drop",
         "rx_ore","rx_fe","rx_ne","rx_defer",
         "can1_tx","can1_rx","can1_err","can2_tx","can2_rx","can2_err",
         "can_rx_drop","can_tx_full"]

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
    gap  = float(sys.argv[2]) if len(sys.argv) > 2 else 5.0
    ser = serial.Serial(port, 115200, timeout=0.2)
    time.sleep(1.0)
    c0 = snapshot(ser)
    if not c0:
        print("快照读取失败")
        sys.exit(2)
    time.sleep(gap)
    ser.baudrate = 115200          # 先变走: 魔法波特率连续设置不触发线控事件(memcmp去重)
    time.sleep(0.2)
    c1 = snapshot(ser)
    ser.close()
    if not c1:
        print("第二次快照读取失败")
        sys.exit(2)

    d = {k: c1[k]-c0[k] for k in NAMES}
    print(f"== CAN双路回环 {gap}s 增量 ==")
    for k in ["can1_tx","can1_rx","can1_err","can2_tx","can2_rx","can2_err",
              "can_rx_drop","can_tx_full"]:
        print(f"  {k:12s} +{d[k]}")
    skew1 = abs(d["can1_tx"] - d["can1_rx"])
    skew2 = abs(d["can2_tx"] - d["can2_rx"])
    print(f"  在途偏差: CAN1 {skew1}  CAN2 {skew2} (快照边界正常<16)")
    ok = (skew1 <= 16 and skew2 <= 16 and
          d["can1_err"] == 0 and d["can2_err"] == 0 and
          d["can_rx_drop"] == 0 and d["can_tx_full"] == 0 and
          d["can1_tx"] > 0 and d["can2_tx"] > 0)
    rate1 = d["can1_tx"]/gap
    rate2 = d["can2_tx"]/gap
    print(f"  帧率: CAN1 {rate1:.0f}fps  CAN2 {rate2:.0f}fps")
    print("结论:", "PASS - 双路硬件回环零丢失" if ok else "FAIL!")
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
