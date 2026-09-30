/**
 * @file bsp_pool.c
 * @author awei (heqiweii@163.com)
 * @brief 块缓冲池实现
 * @version 0.1
 * @date 2026-09-29
 *
 * @copyright Copyright (c) 2026
 *
 * 并发说明: acquire/submit 由RX侧ISR调用, pop/recycle 由消费线程与USB ISR调用,
 * 多生产者多消费者, 统一用关中断保护; 临界区内只有十几条索引/状态指令。
 */
#include "bsp_pool.h"
#include <rthw.h>
#include <string.h>

bsp_dbg_t g_dbg;

static uint8_t bsp_pool_block_idx(bsp_pool_t *pool, uint8_t *block)
{
    return (uint8_t)((block - pool->storage) / pool->block_size);
}

void bsp_pool_init(bsp_pool_t *pool, void *storage, uint16_t block_size, uint8_t count)
{
    rt_memset(pool, 0, sizeof(bsp_pool_t));
    pool->storage = (uint8_t *)storage;
    pool->block_size = block_size;
    pool->count = count;
    for (uint8_t i = 0; i < count; i++) {
        pool->state[i] = BSP_POOL_FREE;
        pool->free_stack[i] = i;
    }
    pool->free_top = count;
}

uint8_t *bsp_pool_acquire(bsp_pool_t *pool)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (pool->free_top == 0) {
        rt_hw_interrupt_enable(level);
        return RT_NULL;
    }
    uint8_t idx = pool->free_stack[--pool->free_top];
    if (idx >= pool->count) {
        /* free_stack被踩: 记录进黑匣子(借用crash区[8..11])并死循环保留现场 */
        volatile uint32_t *box = (volatile uint32_t *)0x2000F000;
        box[8] = 0x5000C0DE;
        box[9] = (uint32_t)pool;
        box[10] = idx;
        box[11] = pool->free_top;
        while (1);
    }
    pool->state[idx] = BSP_POOL_LANDING;
    rt_hw_interrupt_enable(level);
    return pool->storage + (uint16_t)idx * pool->block_size;
}

void bsp_pool_submit(bsp_pool_t *pool, uint8_t *block, uint16_t len)
{
    rt_base_t level = rt_hw_interrupt_disable();
    uint8_t idx = bsp_pool_block_idx(pool, block);
    if (pool->state[idx] != BSP_POOL_LANDING) {
        g_dbg.submit_drop++;
        rt_hw_interrupt_enable(level);
        return;
    }
    pool->state[idx] = BSP_POOL_READY;
    pool->ready_fifo[pool->ready_wt].idx = idx;
    pool->ready_fifo[pool->ready_wt].len = len;
    pool->ready_wt = (pool->ready_wt + 1) % pool->count;
    pool->ready_cnt++;
    rt_hw_interrupt_enable(level);
}

uint8_t bsp_pool_pop(bsp_pool_t *pool, uint8_t **block, uint16_t *len)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (pool->ready_cnt == 0) {
        rt_hw_interrupt_enable(level);
        return 0;
    }
    bsp_pool_desc_t *desc = &pool->ready_fifo[pool->ready_rd];
    pool->ready_rd = (pool->ready_rd + 1) % pool->count;
    pool->ready_cnt--;
    if (pool->state[desc->idx] == BSP_POOL_READY) {
        pool->state[desc->idx] = BSP_POOL_OUT;
    }
    *block = pool->storage + (uint16_t)desc->idx * pool->block_size;
    *len = desc->len;
    rt_hw_interrupt_enable(level);
    return 1;
}

void bsp_pool_recycle(bsp_pool_t *pool, uint8_t *block)
{
    rt_base_t level = rt_hw_interrupt_disable();
    uint8_t idx = bsp_pool_block_idx(pool, block);
    if (pool->state[idx] == BSP_POOL_OUT) {
        pool->state[idx] = BSP_POOL_FREE;
        pool->free_stack[pool->free_top++] = idx;
    } else {
        g_dbg.recycle_drop++;
    }
    rt_hw_interrupt_enable(level);
}

void bsp_pool_reset(bsp_pool_t *pool, uint8_t **except, uint8_t except_n)
{
    rt_base_t level = rt_hw_interrupt_disable();
    for (uint8_t i = 0; i < pool->count; i++) {
        pool->state[i] = BSP_POOL_FREE;
        pool->free_stack[i] = i;
    }
    pool->free_top = pool->count;
    pool->ready_rd = 0;
    pool->ready_wt = 0;
    pool->ready_cnt = 0;
    for (uint8_t e = 0; e < except_n; e++) {
        if (except[e] == RT_NULL) {
            continue;
        }
        uint8_t idx = bsp_pool_block_idx(pool, except[e]);
        pool->state[idx] = BSP_POOL_OUT;
        for (uint8_t i = 0; i < pool->free_top; i++) {
            if (pool->free_stack[i] == idx) {
                pool->free_stack[i] = pool->free_stack[--pool->free_top];
                break;
            }
        }
    }
    rt_hw_interrupt_enable(level);
}
