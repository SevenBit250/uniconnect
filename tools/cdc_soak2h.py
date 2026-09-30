#!/usr/bin/env python3
"""2小时真实负载压测: 四阶段(满载/小包高频/等差+sin+随机混合/超大), 每批MD5校验
用法: py cdc_soak2h.py [PORT] [BAUD] [TOTAL_SECONDS] [RESULT_FILE]
默认: COM5 5000000 7200 tools/soak_result.txt
阶段划分按总时长均分: 满载 -> 小包高频 -> 混合(等差/sin/随机) -> 超大数据"""
import sys, time, hashlib, threading, math, random, ctypes
import serial

PORT_DEFAULT = "COM5"
DBG_MAGIC_BAUD = 1234567
DBG_FIELDS = 29   # g_dbg快照 u32 字段数(25原有+ore/fe/ne/defer)

def pattern(n, seed=0):
    x = ((seed*2654435761+1)&0xFFFFFFFF) or 1
    out = bytearray(n)
    for i in range(n):
        x = (x*1103515245+12345)&0xFFFFFFFF
        out[i] = (x>>16)&0xFF
    return bytes(out)

def snapshot(ser):
    """魔法波特率读固件计数器, 返回u32列表或None"""
    try:
        ser.baudrate = DBG_MAGIC_BAUD
        time.sleep(0.5)
        raw = bytearray()
        end = time.time() + 3
        need = DBG_FIELDS * 4
        while len(raw) < need and time.time() < end:
            c = ser.read(need - len(raw))
            if c:
                raw += c
        if len(raw) < need:
            return None
        import struct
        return list(struct.unpack("<" + "I"*DBG_FIELDS, bytes(raw)))
    except Exception:
        return None

class Soak:
    def __init__(self, port, baud):
        self.port, self.baud = port, baud
        # 4个4MB基型, 任意批内容=基型前缀(确定性), md5按需计算+缓存
        self.base = [pattern(4*1024*1024, 0x5A0000+i) for i in range(4)]
        self.md5_cache = {}
        self.ser = None
        # 统计
        self.reset_stats()

    def reset_stats(self):
        self.batches = self.passes = self.fails = self.werrs = 0
        self.tx = self.rx = 0
        self.fail_log = []

    def open(self):
        self.ser = serial.Serial(self.port, 115200, timeout=0.2, write_timeout=30)
        time.sleep(1.0)
        self.ser.baudrate = self.baud
        time.sleep(0.8)
        self.ser.reset_input_buffer()
        self.ser.read(65536)
        self.ser.reset_input_buffer()

    def md5of(self, variant, size):
        key = (variant, size)
        h = self.md5_cache.get(key)
        if h is None:
            h = hashlib.md5(self.base[variant & 3][:size]).hexdigest()
            self.md5_cache[key] = h
        return h

    def batch(self, size, variant, tag):
        """单批: 写size字节(>=16KB并发线程写) 读回size字节 md5校验"""
        p = self.base[variant & 3][:size]
        got = bytearray()
        werr = [0]
        t0 = time.time()
        if size >= 16384:
            def w():
                try:
                    for i in range(0, size, 4096):
                        self.ser.write(p[i:i+4096])
                        self.ser.flush()
                except Exception:
                    werr[0] = 1
            th = threading.Thread(target=w)
            th.start()
        else:
            try:
                self.ser.write(p)
                self.ser.flush()
            except Exception:
                werr[0] = 1
        wire = size * 10 / self.baud
        deadline = t0 + max(2*wire + 15, 5)
        while len(got) < size and time.time() < deadline:
            got += self.ser.read(min(16384, size - len(got)))
        if size >= 16384:
            th.join(10)

        self.batches += 1
        self.tx += size
        self.rx += len(got)
        if werr[0]:
            self.werrs += 1
        if len(got) == size and hashlib.md5(bytes(got)).hexdigest() == self.md5of(variant, size):
            self.passes += 1
            return True
        self.fails += 1
        mm = next((i for i in range(min(len(got), size)) if got[i] != p[i]), -1)
        self.fail_log.append((tag, self.batches, size, len(got), mm))
        print(f"  [FAIL][{tag}] 批{self.batches}: size={size} 收到{len(got)}B 首异@{mm}", flush=True)
        try:
            self.ser.reset_input_buffer()   # 重同步
        except Exception:
            pass
        return False

    def run_phase(self, name, dur, gen):
        print(f"\n===== 阶段: {name} ({dur}s) =====", flush=True)
        self.reset_stats()
        t_start = time.time()
        t_end = t_start + dur
        next_prog = t_start + 60
        vi = 0
        while time.time() < t_end:
            size = gen()
            if size < 1:
                continue
            self.batch(int(size), vi & 3, name)
            vi += 1
            now = time.time()
            if now >= next_prog:
                next_prog = now + 60
                el = now - t_start
                print(f"  [{name} {el:5.0f}s] 批{self.batches} 过{self.passes} 败{self.fails} "
                      f"TX {self.tx/1048576:.1f}MB RX {self.rx/1048576:.1f}MB", flush=True)
        dt = time.time() - t_start
        print(f"  -- {name} 小计: {dt:.0f}s 批{self.batches} 过{self.passes} 败{self.fails} "
              f"TX {self.tx}B RX {self.rx}B 写异常{self.werrs}", flush=True)
        return dict(name=name, dur=dt, batches=self.batches, passes=self.passes,
                    fails=self.fails, tx=self.tx, rx=self.rx, werrs=self.werrs,
                    fails_detail=list(self.fail_log[:20]))

def main():
    port      = sys.argv[1] if len(sys.argv) > 1 else PORT_DEFAULT
    baud      = int(sys.argv[2]) if len(sys.argv) > 2 else 5000000
    total     = int(sys.argv[3]) if len(sys.argv) > 3 else 7200
    result_fn = sys.argv[4] if len(sys.argv) > 4 else "tools/soak_result.txt"

    try:
        ctypes.windll.kernel32.SetThreadExecutionState(0x80000003)
    except Exception:
        pass

    sk = Soak(port, baud)
    sk.open()
    cnt0 = snapshot(sk.ser)
    if cnt0:
        print(f"起始计数器: {cnt0}", flush=True)
    sk.ser.baudrate = baud           # 魔法波特率后恢复工作波特率
    time.sleep(0.5)
    sk.ser.reset_input_buffer()

    pdur = total // 4

    # 阶段1: 满载 — 1MB连续
    g1 = (lambda: 1048576)
    # 阶段2: 小包高频 — 1..64B 随机
    g2 = (lambda: random.randint(1, 64))
    # 阶段3: 混合 — 等差(1B起+1B至64KB循环)/sin(1B..512KB,60s周期)/log随机(1B..1MB)
    st = {"ap": 0}
    def g_ap():
        st["ap"] = 1 if st["ap"] >= 65536 else st["ap"] + 1
        return st["ap"]
    t3 = [None]
    def g_sin():
        t = time.time() - t3[0]
        return 1 + int(511*1024 * (0.5 + 0.5*math.sin(2*math.pi*t/60)))
    def g_rnd():
        return max(1, min(1000000, int(10 ** random.uniform(0, 6))))
    def g3():
        m = sk.batches % 3
        return (g_ap, g_sin, g_rnd)[m]()
    # 阶段4: 超大 — 4MB
    g4 = (lambda: 4194304)

    t_all = time.time()
    results = []
    results.append(sk.run_phase("满载", pdur, g1))
    results.append(sk.run_phase("小包高频", pdur, g2))
    t3[0] = time.time()
    results.append(sk.run_phase("混合", pdur, g3))
    results.append(sk.run_phase("超大", pdur, g4))

    cnt1 = snapshot(sk.ser)
    if cnt1:
        print(f"结束计数器: {cnt1}", flush=True)
    sk.ser.close()

    total_s = time.time() - t_all
    L = []
    L.append("===== 2小时真实负载压测报告 =====")
    L.append(f"端口/波特率: {port} @ {baud}   实际时长: {total_s:.1f}s ({total_s/3600:.2f}h)")
    gtx = sum(r["tx"] for r in results); grx = sum(r["rx"] for r in results)
    gb = sum(r["batches"] for r in results); gp = sum(r["passes"] for r in results)
    gf = sum(r["fails"] for r in results); gw = sum(r["werrs"] for r in results)
    for r in results:
        L.append(f"[{r['name']}] {r['dur']:.0f}s 批{r['batches']} 过{r['passes']} 败{r['fails']} "
                 f"TX {r['tx']/1048576:.1f}MiB RX {r['rx']/1048576:.1f}MiB 写异常{r['werrs']}"
                 + (f" 明细{r['fails_detail']}" if r['fails'] else ""))
    L.append("----- 总计 -----")
    L.append(f"总批次: {gb} (通过{gp} / 失败{gf})   写异常: {gw}")
    L.append(f"总发送量: {gtx} B = {gtx/1048576:.2f} MiB = {gtx/1e9:.3f} GB")
    L.append(f"总接收量: {grx} B = {grx/1048576:.2f} MiB = {grx/1e9:.3f} GB")
    L.append(f"双向合计: {(gtx+grx)/1e9:.3f} GB   全程平均单向吞吐: {gtx/total_s/1024:.1f} KB/s")
    L.append(f"结论: {'四阶段全部MD5校验通过, 零丢失零异常' if gf==0 and gw==0 else f'{gf}批校验失败/{gw}写异常!'}")
    report = "\n".join(L)
    print("\n" + report, flush=True)
    with open(result_fn, "w", encoding="utf-8") as f:
        f.write(report + "\n")
    sys.exit(0 if gf == 0 and gw == 0 else 1)

if __name__ == "__main__":
    main()
