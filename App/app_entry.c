/* 本文件曾是 SWD 占位线程(swd_thread)的自启动入口(2026-10-01 清理删除)。
 * SWD/DAP 的工作全部由 App/dap_thread.c 的命令泵线程承担, 空占位线程
 * (1KB栈+静态TCB, 当年内存紧张期净占1.2KB)已无意义。
 * 文件保留为空壳以免 MRS 工程源列表变动, 新的 App 层自启动逻辑可放此处。 */

typedef int app_entry_placeholder_t;   /* 空翻译单元占位 */
