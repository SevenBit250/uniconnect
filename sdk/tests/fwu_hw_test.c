/* fwu_hw_test.c -- uniconnect_upgrade 真机测试(需设备在线, 不进CTest默认集)
 *
 * 场景(对照 tools/fw_update.py 差分; 镜像用当前 build/uniconnect.bin = 幂等重刷):
 *   1. 完整升级: APP态触发 -> boot传输 -> REBOOT -> 回到APP态 -> CAN可用
 *   2. 中断注入: 升级到50%时进度回调返回中止 -> 设备须留在boot态
 *   3. 中断恢复: 再跑 fwu_upgrade_file(检测已在boot, 免触发) -> 完成 -> CAN可用
 * 全部PASS退出码0。用法: fwu_hw_test <镜像.bin> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "uniconnect_upgrade.h"
#include "uniconnect_can.h"

static int fails;

#define CHECK(cond, ...) do { \
    if (!(cond)) { fails++; printf("  FAIL @%d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static void msleep(unsigned ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

static int wait_state(int want, const char *name, unsigned timeout_ms)
{
    for (unsigned i = 0; i < timeout_ms / 300u; i++) {
        if (fwu_device_state() == want) {
            printf("  %s: 在线(等待~%ums)\n", name, i * 300u);
            return 0;
        }
        msleep(300);
    }
    return -1;
}

static int cb_progress(uint32_t done, uint32_t total, void *user)
{
    static int last_pct = -1;
    (void)user;
    int pct = (int)(done * 100u / total);
    if (pct != last_pct && pct % 5 == 0) {
        last_pct = pct;
        printf("%d%% ", pct);
    }
    return 0;
}

static int cb_abort50(uint32_t done, uint32_t total, void *user)
{
    (void)user;
    if (done >= total / 2) {
        printf("\n  [注入] 50%%处中止传输\n");
        return 1;                  /* 触发 UNI_ERR_ABORTED */
    }
    return 0;
}

static int can_sanity(void)
{
    can_ctx *c = NULL;
    int r = can_open(&c);
    if (r != UNI_OK) return r;
    uint8_t proto = 0, ch = 0;
    r = can_get_info(c, &proto, &ch);
    can_close(c);
    if (r != UNI_OK) return r;
    return (proto == 1 && ch == 2) ? UNI_OK : UNI_ERR_PROTO;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) {
        printf("用法: fwu_hw_test <镜像.bin>  (通常 build/uniconnect.bin)\n");
        return 2;
    }
    const char *img = argv[1];
    char err[256] = {0};

    /* 前置: 设备须在APP态 */
    int st = fwu_device_state();
    printf("前置: 设备状态=%d (0=APP 1=boot)\n", st);
    if (st != 0) {
        printf("设备不在APP态, 中断恢复场景请先手动恢复\n");
        return 2;
    }

    /* ---- 1. 完整升级 ---- */
    printf("\n[1] 完整升级 %s\n", img);
    int r = fwu_upgrade_file(img, cb_progress, NULL, err, sizeof err);
    printf("\n  rc=%d err=%s\n", r, err);
    CHECK(r == UNI_OK, "升级失败: %d(%s)", r, err);
    CHECK(wait_state(0, "升级后APP", 15000) == 0, "升级后设备未回APP态");
    r = can_sanity();
    CHECK(r == UNI_OK, "升级后CAN不可用: %d(%s)", r, can_strerror(r));
    printf("  升级后CAN: %s\n", r == UNI_OK ? "OK" : "FAIL");

    /* ---- 2. 中断注入: 50%中止 ---- */
    printf("\n[2] 中断注入(50%%中止)\n");
    err[0] = 0;
    r = fwu_upgrade_file(img, cb_abort50, NULL, err, sizeof err);
    printf("  rc=%d(%s)\n", r, fwu_strerror(r));
    CHECK(r == UNI_ERR_ABORTED, "应被回调中止, rc=%d", r);
    /* 设备须留在boot态(P0: 无GOOD记录永不跳转) */
    int in_boot = 0;
    for (int i = 0; i < 10; i++) {
        st = fwu_device_state();
        if (st == 1) { in_boot = 1; break; }
        msleep(500);
    }
    CHECK(in_boot, "中止后设备不在boot态(st=%d) -- 可能跳进半写APP!", st);
    printf("  中止后设备: %s\n", in_boot ? "留boot(正确)" : "FAIL");

    /* ---- 3. 中断恢复: 免触发完整重做 ---- */
    printf("\n[3] 中断恢复(重跑升级)\n");
    err[0] = 0;
    r = fwu_upgrade_file(img, cb_progress, NULL, err, sizeof err);
    printf("\n  rc=%d err=%s\n", r, err);
    CHECK(r == UNI_OK, "恢复升级失败: %d(%s)", r, err);
    CHECK(wait_state(0, "恢复后APP", 15000) == 0, "恢复后设备未回APP态");
    r = can_sanity();
    CHECK(r == UNI_OK, "恢复后CAN不可用: %d(%s)", r, can_strerror(r));
    printf("  恢复后CAN: %s\n", r == UNI_OK ? "OK" : "FAIL");

    printf("\n== %s (fails=%d) ==\n", fails ? "FAIL" : "全部PASS", fails);
    return fails ? 1 : 0;
}
