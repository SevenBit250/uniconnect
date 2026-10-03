#!/usr/bin/env python3
"""amalgamate.py -- SDK 交付头聚合: 5头(源) -> 3头(交付)

交付物只含 uniconnect_common.h / uniconnect_upgrade.h / uniconnect_can.h 三个文件:
固件同源头(usb_ids.h/can_proto.h)的内容在打包时内联进
uniconnect_common.h/uniconnect_can.h, 单一事实源仍固件侧(源头 #include 不变),
本脚本是纯文本搬移(整文件原样嵌入, 含include guard, 嵌套无害)。
"""
import os
import re
import sys

SDK_INC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "include")
FW_COMMON = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common")


def inline_includes(text, fw_headers_src):
    """把 #include "xxx.h" 中属于固件头的行替换为该文件全文(带来源注释放头尾)"""
    def repl(m):
        name = m.group(1)
        path = os.path.join(fw_headers_src, name)
        if not os.path.exists(path):
            return m.group(0)  # 非固件头(如 uniconnect_common.h)原样保留
        body = open(path, encoding="utf-8").read().rstrip()
        return (f"/* ---- 以下内容内联自固件 common/{name} (单一事实源, 勿在此改) ---- */\n"
                f"{body}\n"
                f"/* ---- 内联结束 ---- */")
    return re.sub(r'^#include "(\w+\.h)".*$', repl, text, flags=re.M)


def main():
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    banner = ("/* [聚合生成, 勿手改] 由 sdk/amalgamate.py 从 sdk/include 与固件 common/ 生成,\n"
              " * 交付面即本目录三个头文件, 无外部依赖。 */\n")
    for name in ("uniconnect_common.h", "uniconnect_upgrade.h", "uniconnect_can.h"):
        src = open(os.path.join(SDK_INC, name), encoding="utf-8").read()
        merged = banner + inline_includes(src, FW_COMMON)
        open(os.path.join(out_dir, name), "w", encoding="utf-8", newline="\n").write(merged)
        print(f"amalgamate: {name} -> {out_dir}")


if __name__ == "__main__":
    main()
