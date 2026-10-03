/* test_abi.c -- SDK 冒烟测试: 两库加载/ABI版本/生命周期往返 */
#include <stdio.h>
#include "uniconnect_upgrade.h"
#include "uniconnect_can.h"

int main(void)
{
    int rc = 0;
    if (fwu_abi_version() != UNI_ABI_VERSION) { puts("fwu abi ver FAIL"); rc = 1; }
    if (can_abi_version() != UNI_ABI_VERSION) { puts("can abi ver FAIL"); rc = 1; }
    if (!fwu_strerror(UNI_OK) || !can_strerror(UNI_OK)) { puts("strerror FAIL"); rc = 1; }

    fwu_ctx *f = NULL;
    if (fwu_open(&f) != UNI_OK || !f) { puts("fwu_open FAIL"); rc = 1; }
    else fwu_close(f);

    can_ctx *c = NULL;
    if (can_open(&c) != UNI_OK || !c) { puts("can_open FAIL"); rc = 1; }
    else can_close(c);

    puts(rc == 0 ? "test_abi PASS" : "test_abi FAIL");
    return rc;
}
