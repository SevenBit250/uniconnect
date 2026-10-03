/* can_hw_test.c -- uniconnect_can 真机测试(需设备在线, 不进CTest默认集)
 *
 * 场景(对照 tools/can_usb_test.py 差分):
 *   1. open + GET_INFO 协议握手
 *   2. 双通道静默回环: 各发N帧 -> 收N帧, 逐帧内容校验(id/dlc/data哈希) + 设备计数对账
 *   3. 正常模式交叉(需CAN1/2对插): CH0发->CH1收, CH1发->CH0收, 逐帧校验
 *   4. 语义检查: recv超时/0超时/flush_rx
 * 全部PASS退出码0。用法: can_hw_test [每通道帧数] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uniconnect_can.h"

static int fails;

#define CHECK(cond, ...) do { \
    if (!(cond)) { fails++; printf("  FAIL @%d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static uint32_t hash_of(uint32_t h)
{
    return h * 2654435761u;
}

static void fill_frame(can_frame *f, uint32_t id, uint32_t seq)
{
    f->id = id;
    f->flags = 0;
    f->dlc = 8;
    memcpy(f->data, &seq, 4);
    uint32_t hs = hash_of(seq);
    memcpy(f->data + 4, &hs, 4);
}

/* 发n帧并收n帧回环校验(单通道) */
static void loopback_ch(can_ctx *c, uint8_t ch, uint32_t base_id, int n)
{
    printf("[%s] CH%u 静默回环 %d帧...\n", __func__, ch, n);
    for (int i = 0; i < n; i++) {
        can_frame f;
        fill_frame(&f, base_id, (uint32_t)i + 1);
        int r = can_send(c, ch, &f, 1000);
        CHECK(r == UNI_OK, "CH%u send#%d rc=%d(%s)", ch, i, r, can_strerror(r));
        if (r != UNI_OK) return;
    }
    int got = 0;
    for (int i = 0; i < n; i++) {
        can_frame f;
        int r = can_recv(c, ch, &f, 500);
        if (r != UNI_OK) { CHECK(0, "CH%u recv#%d rc=%d(%s)", ch, i, r, can_strerror(r)); break; }
        uint32_t seq;
        memcpy(&seq, f.data, 4);
        uint32_t hs;
        memcpy(&hs, f.data + 4, 4);
        CHECK(f.id == base_id && f.dlc == 8 && seq == (uint32_t)i + 1 && hs == hash_of(seq),
              "CH%u 帧内容错#%d id=%03x dlc=%u seq=%u", ch, i, f.id, f.dlc, seq);
        got++;
    }
    printf("  收到 %d/%d\n", got, n);
}

/* 正常模式交叉: tx_ch发 -> rx_ch收(需CAN1/2对插的真总线) */
static void cross_txrx(can_ctx *c, uint8_t tx_ch, uint8_t rx_ch, uint32_t base_id, int n)
{
    printf("[%s] CH%u->CH%u 交叉 %d帧...\n", __func__, tx_ch, rx_ch, n);
    for (int i = 0; i < n; i++) {
        can_frame f;
        fill_frame(&f, base_id, (uint32_t)i + 1);
        if (i % 50 == 0 || i == n - 1) printf("  [%u->%u] #%d/%d...", tx_ch, rx_ch, i + 1, n);
        int r = can_send(c, tx_ch, &f, 1000);
        if (i % 50 == 0 || i == n - 1) printf("send=%d ", r);
        CHECK(r == UNI_OK, "CH%u send#%d rc=%d(%s)", tx_ch, i, r, can_strerror(r));
        if (r != UNI_OK) return;
        /* 边发边收: 接收通道环不积压(也验证send等待期pump并发收割) */
        can_frame g;
        r = can_recv(c, rx_ch, &g, 500);
        CHECK(r == UNI_OK, "CH%u recv#%d rc=%d(%s)", rx_ch, i, r, can_strerror(r));
        if (r == UNI_OK) {
            uint32_t seq; memcpy(&seq, g.data, 4);
            uint32_t hs;  memcpy(&hs, g.data + 4, 4);
            CHECK(g.id == base_id && g.dlc == 8 && seq == (uint32_t)i + 1 && hs == hash_of(seq),
                  "CH%u 交叉帧内容错#%d id=%03x seq=%u", rx_ch, i, g.id, seq);
        }
    }
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* 真机调试: 无缓冲, 卡死也能看到现场 */
    int n = argc > 1 ? atoi(argv[1]) : 200;
    can_ctx *c = NULL;
    printf("open...\n");
    int r = can_open(&c);
    if (r != UNI_OK) {
        printf("open失败: %d(%s)\n", r, can_strerror(r));
        return 2;
    }
    printf("设备已打开(接口%d 端点0x%02x/0x%02x), 每通道%d帧\n\n", 3, 0x06, 0x83, n);

    /* 1. 协议握手(can_open已GET_INFO对账, 再显式读一次) */
    uint8_t proto = 0, ch_num = 0;
    r = can_get_info(c, &proto, &ch_num);
    CHECK(r == UNI_OK && proto == CAN_PROTO_VER && ch_num == 2,
          "GET_INFO rc=%d proto=%u ch=%u", r, proto, ch_num);
    printf("GET_INFO: proto=%u ch_num=%u\n", proto, ch_num);

    /* 计数基数(排除遗留会话增量) */
    can_ch_status s0[2];
    for (int ch = 0; ch < 2; ch++) {
        r = can_get_status(c, (uint8_t)ch, &s0[ch]);
        CHECK(r == UNI_OK, "GET_STATUS base CH%d rc=%d", ch, r);
    }

    /* 2. 静默回环(不驱动总线) */
    for (int ch = 0; ch < 2; ch++) {
        r = can_set_bitrate(c, (uint8_t)ch, 1000);
        CHECK(r == UNI_OK, "CH%d SET_BITRATE rc=%d(%s)", ch, r, can_strerror(r));
        r = can_set_mode(c, (uint8_t)ch, CAN_MODE_LOOPBACK_TEST);
        CHECK(r == UNI_OK, "CH%d SET_MODE(loop) rc=%d(%s)", ch, r, can_strerror(r));
    }
    loopback_ch(c, 0, 0x100, n);
    loopback_ch(c, 1, 0x140, n);

    /* 回环计数对账 */
    for (int ch = 0; ch < 2; ch++) {
        can_ch_status s;
        r = can_get_status(c, (uint8_t)ch, &s);
        CHECK(r == UNI_OK, "GET_STATUS CH%d rc=%d", ch, r);
        uint32_t dtx = s.tx_cnt - s0[ch].tx_cnt, drx = s.rx_cnt - s0[ch].rx_cnt;
        printf("CH%d 回环计数增量: tx=%u rx=%u err=%u\n", ch, dtx, drx, s.err_cnt);
        CHECK(dtx >= (uint32_t)n && drx >= (uint32_t)n, "CH%d 计数不足 tx=%u rx=%u", ch, dtx, drx);
        CHECK(s.err_cnt == 0, "CH%d err=%u 非零", ch, s.err_cnt);
    }

    /* 3. 正常模式交叉(真总线, CAN1/2对插) */
    printf("\n切正常模式(真总线)...\n");
    for (int ch = 0; ch < 2; ch++) {
        r = can_set_mode(c, (uint8_t)ch, CAN_MODE_NORMAL);
        CHECK(r == UNI_OK, "CH%d SET_MODE(normal) rc=%d(%s)", ch, r, can_strerror(r));
    }
    cross_txrx(c, 0, 1, 0x200, n);
    cross_txrx(c, 1, 0, 0x240, n);

    /* 4. 语义检查 */
    printf("\n语义检查...\n");
    can_frame f;
    r = can_recv(c, 0, &f, 200);
    CHECK(r == UNI_ERR_TIMEOUT, "静默期recv应超时, rc=%d", r);
    printf("  recv(200ms)=%d;", r);
    r = can_recv(c, 0, &f, 0);
    CHECK(r == UNI_ERR_TIMEOUT, "0超时recv应立即超时, rc=%d", r);
    printf(" recv(0)=%d;", r);
    r = can_flush_rx(c, 0);
    CHECK(r == UNI_OK, "flush_rx rc=%d", r);
    printf(" flush=%d\n", r);

    /* 收工: 回睡眠(省电/静默) */
    printf("收工: SLEEP+close...\n");
    for (int ch = 0; ch < 2; ch++)
        (void)can_set_mode(c, (uint8_t)ch, CAN_MODE_SLEEP);
    can_close(c);
    printf("closed.\n");

    printf("\n== %s (fails=%d) ==\n", fails ? "FAIL" : "全部PASS", fails);
    return fails ? 1 : 0;
}
