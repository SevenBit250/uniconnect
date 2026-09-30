/**
 * @file bsp_pool.h
 * @author awei (heqiweii@163.com)
 * @brief 块缓冲池: 固定尺寸块的零拷贝生产者/消费者交接
 * @version 0.1
 * @date 2026-09-29
 *
 * @copyright Copyright (c) 2026
 *
 * 设计约束:
 * - 块首地址恒定 4 字节对齐, 可直接作为 USBHS 等字宽 DMA 的缓冲地址
 * - 所有权单向流转, 每块带状态字节, 非法转换一律拒绝(防重复回收/重复提交)
 * - 所有操作临界区内只做索引/状态运算, 绝无 memcpy, 单次 <= 0.3us
 * - 可在 ISR 与线程上下文混用; 内部实现将来可替换为无锁版本, API 不变
 */
#ifndef __BSP_POOL_H
#define __BSP_POOL_H

#include <stdint.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 块所有权状态, 单向流转 */
enum {
    BSP_POOL_FREE = 0,   /* 空闲, 在free栈中, 可作为DMA落点 */
    BSP_POOL_LANDING,    /* 生产者DMA正在写入 */
    BSP_POOL_READY,      /* 已提交, 在就绪FIFO中待消费 */
    BSP_POOL_OUT,        /* 已被消费者取出, 传输在途 */
};

#ifndef BSP_POOL_MAX_BLOCKS
#define BSP_POOL_MAX_BLOCKS 16
#endif

typedef struct {
    uint8_t idx;     /* 就绪块编号 */
    uint16_t len;    /* 有效字节数 */
} bsp_pool_desc_t;

typedef struct {
    uint8_t *storage;                                  /* N*block_size 连续存储, 4字节对齐 */
    uint16_t block_size;
    uint8_t count;
    uint8_t state[BSP_POOL_MAX_BLOCKS];                /* 每块当前状态 */
    uint8_t free_stack[BSP_POOL_MAX_BLOCKS];           /* 空闲块索引栈 */
    uint8_t free_top;
    bsp_pool_desc_t ready_fifo[BSP_POOL_MAX_BLOCKS];   /* 就绪描述符FIFO */
    uint8_t ready_rd;
    uint8_t ready_wt;
    uint8_t ready_cnt;
} bsp_pool_t;

void bsp_pool_init(bsp_pool_t *pool, void *storage, uint16_t block_size, uint8_t count);

/* 取空闲块作为DMA落点, FREE->LANDING, 无空闲返回NULL */
uint8_t *bsp_pool_acquire(bsp_pool_t *pool);

/* 提交落块, LANDING->READY, 长度写入就绪FIFO */
void bsp_pool_submit(bsp_pool_t *pool, uint8_t *block, uint16_t len);

/* 取出一个就绪块, READY->OUT, 无就绪返回0 */
uint8_t bsp_pool_pop(bsp_pool_t *pool, uint8_t **block, uint16_t *len);

/* 归还消费完的块, OUT->FREE, 状态不符(如reset后重复回收)静默丢弃 */
void bsp_pool_recycle(bsp_pool_t *pool, uint8_t *block);

/* 重建池: 全部块回FREE并清空就绪FIFO, except中的在途块保留OUT待消费者归还 */
void bsp_pool_reset(bsp_pool_t *pool, uint8_t **except, uint8_t except_n);

/* ---- 调试计数器(全链路事件统计, 魔法波特率1234567读取快照) ---- */
typedef struct {
    /* uart rx 落块通路 */
    uint32_t uart_idle_evt;      // IDLE中断次数
    uint32_t uart_tc_evt;        // 块满TC次数
    uint32_t uart_idle_zero;     // IDLE无数据跳过
    uint32_t uart_submits;       // RX提交块数
    uint32_t uart_pause;         // RX池耗尽暂停次数
    uint32_t uart_resume_ok;     // RX恢复成功
    uint32_t uart_resume_fail;   // RX恢复失败(仍无空闲块)
    /* cdc out 落块通路 */
    uint32_t out_done;           // OUT完成回调次数
    uint32_t out_zlp;            // OUT零长包
    uint32_t out_pause;          // OUT池耗尽挂起
    uint32_t out_resume_ok;
    uint32_t out_resume_fail;
    /* usb in 发送通路 */
    uint32_t in_pop;             // 取块次数
    uint32_t in_recycle;         // 回收次数
    uint32_t in_bytes;           // IN方向实发字节累计(bulk_in完成nbytes累加)
    uint32_t out_bytes;          // USB OUT收到字节累计
    uint32_t tx_bytes;           // UART TX发线字节累计
    uint32_t in_zlp;             // ZLP发送次数
    uint32_t in_write_err;       // start_write返回错误次数
    uint32_t in_write_ret;       // 最近一次错误码
    uint32_t in_write_ptr;       // 最近一次错误时的缓冲指针
    /* uart tx */
    uint32_t tx_dma;             // DMA发送次数
    uint32_t tx_dma_zero;        // 零长防御触发
    /* 池状态机异常 */
    uint32_t recycle_drop;       // 非法回收被丢弃
    uint32_t submit_drop;        // 非法提交被丢弃
} bsp_dbg_t;

extern bsp_dbg_t g_dbg;

#ifdef __cplusplus
}
#endif

#endif /* __BSP_POOL_H */
