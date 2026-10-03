#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""uniconnect_can CAN 通讯 GUI 示例 (Python + tkinter + ctypes)

覆盖 can_comm.h 全部 API: open/close、get_info、set_bitrate、set_mode、
send、recv(轮询显示)、flush_rx、get_status(定时刷新)。
通道独立面板(CH0/CH1), 接收列表滚动展示, 日志栏呈现错误明细。

运行: python can_gui.py   (DLL 目录可用环境变量 UNICONNECT_SDK_DIR 指定)
"""
import ctypes
import os
import queue
import threading
import tkinter as tk
from tkinter import ttk

# ---- SDK 装载(ctypes 映射, 签名见 uniconnect_can.h) ----

class CanFrame(ctypes.Structure):
    _fields_ = [("id", ctypes.c_uint32),
                ("flags", ctypes.c_uint8),
                ("dlc", ctypes.c_uint8),
                ("data", ctypes.c_uint8 * 8)]

FLAG_EXT = 0x01   # 扩展ID(与 uniconnect_can.h 一致)


def load_sdk():
    here = os.path.dirname(os.path.abspath(__file__))
    for d in (os.environ.get("UNICONNECT_SDK_DIR"),
              os.path.join(here, "..", "..", "build", "sdk"),
              here):
        if d and os.path.isfile(os.path.join(d, "uniconnect_can.dll")):
            dll = ctypes.CDLL(os.path.join(d, "uniconnect_can.dll"))
            break
    else:
        raise FileNotFoundError("未找到 uniconnect_can.dll (设置 UNICONNECT_SDK_DIR?)")
    dll.can_abi_version.restype = ctypes.c_uint32
    dll.can_strerror.restype = ctypes.c_char_p
    dll.can_strerror.argtypes = [ctypes.c_int]
    dll.can_open.restype = ctypes.c_int
    dll.can_open.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    dll.can_close.restype = None
    dll.can_close.argtypes = [ctypes.c_void_p]
    dll.can_set_mode.restype = ctypes.c_int
    dll.can_set_mode.argtypes = [ctypes.c_void_p, ctypes.c_uint8, ctypes.c_uint8]
    dll.can_set_bitrate.restype = ctypes.c_int
    dll.can_set_bitrate.argtypes = [ctypes.c_void_p, ctypes.c_uint8, ctypes.c_uint32]
    dll.can_send.restype = ctypes.c_int
    dll.can_send.argtypes = [ctypes.c_void_p, ctypes.c_uint8,
                             ctypes.POINTER(CanFrame), ctypes.c_uint]
    dll.can_recv.restype = ctypes.c_int
    dll.can_recv.argtypes = [ctypes.c_void_p, ctypes.c_uint8,
                             ctypes.POINTER(CanFrame), ctypes.c_uint]
    dll.can_flush_rx.restype = ctypes.c_int
    dll.can_flush_rx.argtypes = [ctypes.c_void_p, ctypes.c_uint8]
    dll.can_get_status.restype = ctypes.c_int

    class ChStatus(ctypes.Structure):
        _fields_ = [("tx_cnt", ctypes.c_uint32),
                    ("rx_cnt", ctypes.c_uint32),
                    ("err_cnt", ctypes.c_uint32)]
    dll.can_get_status.argtypes = [ctypes.c_void_p, ctypes.c_uint8,
                                   ctypes.POINTER(ChStatus)]
    dll.can_get_info.restype = ctypes.c_int
    dll.can_get_info.argtypes = [ctypes.c_void_p,
                                 ctypes.POINTER(ctypes.c_uint8),
                                 ctypes.POINTER(ctypes.c_uint8)]
    return dll


MODES = [("正常(接总线)", 1), ("静默回环(自测)", 0), ("睡眠", 2)]


class ChanPanel(ttk.LabelFrame):
    """单通道面板: 波特率/模式设置 + 状态计数 + 发送"""

    def __init__(self, master, app, ch):
        super().__init__(master, text="CH%d" % ch)
        self.app, self.ch = app, ch
        pad = dict(padx=4, pady=3)

        row1 = ttk.Frame(self); row1.pack(fill="x", **pad)
        ttk.Label(row1, text="波特率(kbps):").pack(side="left")
        self.bitrate = tk.StringVar(value="1000")
        ttk.Entry(row1, textvariable=self.bitrate, width=6).pack(side="left", padx=3)
        ttk.Button(row1, text="设置", width=4,
                   command=lambda: app.op(app.dll.can_set_bitrate,
                                          ch, int(self.bitrate.get()))).pack(side="left")
        ttk.Label(row1, text="  模式:").pack(side="left")
        self.mode = tk.StringVar(value=MODES[0][0])
        ttk.Combobox(row1, textvariable=self.mode, values=[m[0] for m in MODES],
                     state="readonly", width=10).pack(side="left", padx=3)
        ttk.Button(row1, text="设置", width=4, command=self.set_mode).pack(side="left")

        row2 = ttk.Frame(self); row2.pack(fill="x", **pad)
        self.stat = tk.StringVar(value="tx=0 rx=0 err=0")
        ttk.Label(row2, textvariable=self.stat).pack(side="left")
        ttk.Button(row2, text="刷新状态", width=8, command=self.refresh_status).pack(side="right")

        row3 = ttk.Frame(self); row3.pack(fill="x", **pad)
        ttk.Label(row3, text="ID:").pack(side="left")
        self.fid = tk.StringVar(value="123")
        ttk.Entry(row3, textvariable=self.fid, width=8).pack(side="left", padx=2)
        self.ext = tk.BooleanVar(value=False)
        ttk.Checkbutton(row3, text="扩展", variable=self.ext).pack(side="left")
        ttk.Label(row3, text="数据(hex, 空格分隔):").pack(side="left", padx=(8, 0))
        self.data = tk.StringVar(value="01 02 03 04 05 06 07 08")
        ttk.Entry(row3, textvariable=self.data, width=24).pack(side="left", fill="x", expand=True)
        ttk.Button(row3, text="发送", command=self.send).pack(side="left", padx=3)

    def set_mode(self):
        val = dict((name, v) for name, v in MODES)[self.mode.get()]
        self.app.op(self.app.dll.can_set_mode, self.ch, val)

    def refresh_status(self):
        if not self.app.ctx:
            return
        st = self.app.ChStatus()
        rc = self.app.dll.can_get_status(self.app.ctx, self.ch, ctypes.byref(st))
        if rc == 0:
            self.stat.set("tx=%d rx=%d err=%d" % (st.tx_cnt, st.rx_cnt, st.err_cnt))
        else:
            self.app.log("CH%d get_status: %s" % (self.ch, self.app.err(rc)))

    def send(self):
        f = CanFrame()
        try:
            f.id = int(self.fid.get(), 16 if self.ext.get() else 0)
            data = bytes.fromhex(self.data.get())
            f.dlc = len(data)
            f.flags = FLAG_EXT if self.ext.get() else 0
            f.data[:] = data[:8]
        except ValueError:
            self.app.log("发送参数格式错误(ID或数据hex)")
            return
        self.app.op(self.app.dll.can_send, self.ch, ctypes.byref(f), 1000, frame=True)


class App:
    def __init__(self):
        self.dll = load_sdk()
        self.ctx = None
        self.rxq = queue.Queue()

        self.root = tk.Tk()
        self.root.title("UniConnect CAN 工具")
        self.root.minsize(720, 520)
        self._build_ui()
        self._poll()

    def _build_ui(self):
        pad = dict(padx=6, pady=4)
        top = ttk.Frame(self.root); top.pack(fill="x", **pad)
        self.btn_open = ttk.Button(top, text="打开设备", command=self.open_dev)
        self.btn_open.pack(side="left")
        self.btn_close = ttk.Button(top, text="关闭设备", command=self.close_dev, state="disabled")
        self.btn_close.pack(side="left", padx=4)
        self.info_var = tk.StringVar(value="未打开 | 库 ABI v%d" % self.dll.can_abi_version())
        ttk.Label(top, textvariable=self.info_var).pack(side="left", padx=10)
        self.btn_flush = ttk.Button(top, text="清空接收", command=lambda: self.flush(0))
        self.btn_flush.pack(side="right")

        chans = ttk.Frame(self.root); chans.pack(fill="x", **pad)
        self.panels = [ChanPanel(chans, self, 0), ChanPanel(chans, self, 1)]
        for p in self.panels:
            p.pack(side="left", fill="both", expand=True, padx=3)

        rx = ttk.LabelFrame(self.root, text="接收(自动轮询)"); rx.pack(fill="both", expand=True, **pad)
        self.rx_list = tk.Listbox(rx, font=("Consolas", 9))
        self.rx_list.pack(fill="both", expand=True, padx=4, pady=4)

        log = ttk.LabelFrame(self.root, text="日志"); log.pack(fill="x", **pad)
        self.log_text = tk.Text(log, height=6, state="disabled", font=("Consolas", 9))
        self.log_text.pack(fill="x", padx=4, pady=4)

    # ---------- 基础 ----------
    def log(self, msg):
        self.log_text.configure(state="normal")
        self.log_text.insert("end", msg + "\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def err(self, rc):
        return self.dll.can_strerror(rc).decode()

    def op(self, fn, *args, frame=False):
        """统一调用入口: 错误进日志, 成功的收发不刷屏"""
        if not self.ctx and fn not in (self.dll.can_open,):
            self.log("设备未打开")
            return
        rc = fn(self.ctx, *args)
        if rc != 0:
            name = fn.__name__ if hasattr(fn, "__name__") else "op"
            self.log("%s: %s (rc=%d)" % (name, self.err(rc), rc))
        return rc

    # ---------- 设备生命周期 ----------
    def open_dev(self):
        p = ctypes.c_void_p()
        rc = self.dll.can_open(ctypes.byref(p))
        if rc != 0:
            self.log("can_open: %s (rc=%d)" % (self.err(rc), rc))
            return
        self.ctx = p
        self.btn_open.configure(state="disabled")
        self.btn_close.configure(state="normal")
        pv, cn = ctypes.c_uint8(), ctypes.c_uint8()
        rc = self.dll.can_get_info(self.ctx, ctypes.byref(pv), ctypes.byref(cn))
        info = ("proto=v%s 通道数=%s" % (pv.value, cn.value)) if rc == 0 else ("get_info: " + self.err(rc))
        self.info_var.set("已打开 | " + info)
        self.log("设备已打开 (%s)" % info)
        threading.Thread(target=self._rx_worker, daemon=True).start()

    def close_dev(self):
        if self.ctx:
            self.dll.can_close(self.ctx)
            self.ctx = None
        self.btn_open.configure(state="normal")
        self.btn_close.configure(state="disabled")
        self.info_var.set("已关闭 | 库 ABI v%d" % self.dll.can_abi_version())
        self.log("设备已关闭")

    def flush(self, ch):
        self.op(self.dll.can_flush_rx, ch)
        self.log("CH%d 接收已清空(设备侧)" % ch)

    # ---------- 接收轮询(工作线程, 100ms 批量) ----------
    def _rx_worker(self):
        while self.ctx:
            for ch in (0, 1):
                f = CanFrame()
                rc = self.dll.can_recv(self.ctx, ch, ctypes.byref(f), 50)
                if rc == 0:
                    data = " ".join("%02X" % b for b in f.data[:f.dlc])
                    self.rxq.put("CH%d  ID:%s  DLC:%d  %s" %
                                 (ch, ("%08X" % f.id) if f.flags & FLAG_EXT else ("%03X" % f.id),
                                  f.dlc, data))
                elif rc != -6:   # -6=UNI_ERR_TIMEOUT 属正常空轮询
                    self.rxq.put(None) if False else None
                    return

    def _poll(self):
        try:
            while True:
                self.rx_list.insert("end", self.rxq.get_nowait())
                if self.rx_list.size() > 500:
                    self.rx_list.delete(0, 100)
                self.rx_list.see("end")
        except queue.Empty:
            pass
        self.root.after(100, self._poll)


if __name__ == "__main__":
    App().root.mainloop()
