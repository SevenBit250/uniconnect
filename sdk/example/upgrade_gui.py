#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""uniconnect_upgrade 固件升级 GUI 示例 (Python + tkinter + ctypes)

演示 SDK 升级库的标准用法: 选择镜像文件 -> 展示信息(大小/CRC32/预览)
-> 探测设备形态 -> 一把梭升级(进度回调) -> 错误明细展示。

运行: python upgrade_gui.py   (DLL 目录可用环境变量 UNICONNECT_SDK_DIR 指定,
     默认按仓库布局找 ../../build/sdk 与脚本同目录)
"""
import ctypes
import os
import queue
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

# ---- SDK 装载(ctypes 映射, 签名见 uniconnect_upgrade.h) ----

def load_sdk():
    here = os.path.dirname(os.path.abspath(__file__))
    for d in (os.environ.get("UNICONNECT_SDK_DIR"),
              os.path.join(here, "..", "..", "build", "sdk"),
              here):
        if d and os.path.isfile(os.path.join(d, "uniconnect_upgrade.dll")):
            dll = ctypes.CDLL(os.path.join(d, "uniconnect_upgrade.dll"))
            break
    else:
        raise FileNotFoundError("未找到 uniconnect_upgrade.dll (设置 UNICONNECT_SDK_DIR?)")
    dll.fwu_abi_version.restype = ctypes.c_uint32
    dll.fwu_strerror.restype = ctypes.c_char_p
    dll.fwu_strerror.argtypes = [ctypes.c_int]
    dll.fwu_upgrade_file.restype = ctypes.c_int
    dll.fwu_upgrade_file.argtypes = [ctypes.c_char_p, PROGRESS_CB, ctypes.c_void_p,
                                     ctypes.c_char_p, ctypes.c_size_t]
    dll.fwu_device_state.restype = ctypes.c_int
    return dll


PROGRESS_CB = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p)


def crc32(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1))
    return crc ^ 0xFFFFFFFF


class App:
    def __init__(self):
        self.dll = load_sdk()
        self.path = None
        self.image = None
        self.q = queue.Queue()          # 工作线程 -> UI (进度/日志)

        self.root = tk.Tk()
        self.root.title("UniConnect 固件升级")
        self.root.minsize(560, 420)
        self._build_ui()
        self._poll()
        self.refresh_state()

    # ---------- UI ----------
    def _build_ui(self):
        pad = dict(padx=6, pady=4)

        top = ttk.Frame(self.root); top.pack(fill="x", **pad)
        ttk.Label(top, text="库 ABI: v%d" % self.dll.fwu_abi_version()).pack(side="left")
        self.state_var = tk.StringVar(value="设备: 未知")
        ttk.Label(top, textvariable=self.state_var).pack(side="right")
        ttk.Button(top, text="刷新设备状态", command=self.refresh_state).pack(side="right", padx=4)

        f = ttk.LabelFrame(self.root, text="固件镜像"); f.pack(fill="x", **pad)
        row = ttk.Frame(f); row.pack(fill="x", padx=6, pady=4)
        ttk.Button(row, text="选择文件...", command=self.pick_file).pack(side="left")
        self.file_var = tk.StringVar(value="(未选择)")
        ttk.Label(row, textvariable=self.file_var, anchor="w").pack(side="left", fill="x", expand=True, padx=8)
        self.info_var = tk.StringVar(value="")
        ttk.Label(f, textvariable=self.info_var, anchor="w", justify="left").pack(fill="x", padx=6)

        u = ttk.LabelFrame(self.root, text="升级"); u.pack(fill="x", **pad)
        self.btn_go = ttk.Button(u, text="开始升级", command=self.start, state="disabled")
        self.btn_go.pack(padx=6, pady=4, anchor="w")
        self.pb = ttk.Progressbar(u, maximum=100)
        self.pb.pack(fill="x", padx=6, pady=2)
        self.pct_var = tk.StringVar(value="")
        ttk.Label(u, textvariable=self.pct_var).pack(anchor="w", padx=6)

        log = ttk.LabelFrame(self.root, text="日志"); log.pack(fill="both", expand=True, **pad)
        self.log_text = tk.Text(log, height=10, state="disabled", font=("Consolas", 9))
        self.log_text.pack(fill="both", expand=True, padx=4, pady=4)

    def log(self, msg):
        self.log_text.configure(state="normal")
        self.log_text.insert("end", msg + "\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    # ---------- 逻辑 ----------
    def pick_file(self):
        p = filedialog.askopenfilename(
            title="选择固件镜像",
            filetypes=[("固件镜像", "*.bin"), ("所有文件", "*.*")])
        if not p:
            return
        self.path = p
        self.image = open(p, "rb").read()
        head = self.image[:16].hex(" ")
        self.file_var.set(os.path.basename(p))
        self.info_var.set(
            "大小: %d B (%.1f KB)    CRC32: 0x%08X\n路径: %s\n首16字节: %s"
            % (len(self.image), len(self.image) / 1024, crc32(self.image), p, head))
        self.btn_go.configure(state="normal")
        self.log("已选择镜像: %s (%d B)" % (os.path.basename(p), len(self.image)))

    def refresh_state(self):
        st = self.dll.fwu_device_state()
        self.state_var.set({
            0: "设备: APP 态(0x0218, 可触发升级)",
            1: "设备: BOOT 态(0x0219, 等待传输)",
        }.get(st, "设备: 不在线 (%s)" % self.dll.fwu_strerror(st).decode()))

    def start(self):
        if not self.path:
            return
        self.btn_go.configure(state="disabled")
        self.pb["value"] = 0
        self.log("开始升级: %s" % self.path)
        threading.Thread(target=self._worker, daemon=True).start()

    def _worker(self):
        # 进度回调在SDK线程执行: 只入队, UI线程_poll里消费
        @PROGRESS_CB
        def cb(done, total, _user):
            self.q.put(("prog", done, total))
            return 0

        err = ctypes.create_string_buffer(256)
        rc = self.dll.fwu_upgrade_file(self.path.encode("utf-8"), cb, None, err, len(err))
        self.q.put(("done", rc, err.value.decode("utf-8", "replace")))

    def _poll(self):
        try:
            while True:
                item = self.q.get_nowait()
                if item[0] == "prog":
                    _, done, total = item
                    pct = done * 100 // max(total, 1)
                    self.pb["value"] = pct
                    self.pct_var.set("传输 %d / %d B (%d%%)" % (done, total, pct))
                elif item[0] == "done":
                    _, rc, detail = item
                    if rc == 0:
                        self.pb["value"] = 100
                        self.pct_var.set("完成")
                        self.log("升级成功, 设备已重启回 APP")
                        messagebox.showinfo("UniConnect", "升级完成")
                    else:
                        self.log("升级失败: %s (rc=%d%s)" %
                                 (self.dll.fwu_strerror(rc).decode(), rc,
                                  ("; " + detail) if detail else ""))
        except queue.Empty:
            pass
        self.root.after(100, self._poll)


if __name__ == "__main__":
    App().root.mainloop()
