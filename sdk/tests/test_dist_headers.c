/* test_dist_headers.c -- 交付头自包含验证: 仅以聚合目录为include路径编译,
 * 三个头互不依赖外部文件(任何缺文件/残留外部include都会在此炸) */
#include "uniconnect_common.h"
#include "uniconnect_upgrade.h"
#include "uniconnect_can.h"

int main(void)
{
    /* 聚合进来的固件常量可达性抽查 */
    return (UNI_VID == 0x0D28 && CAN_MODE_SLEEP == 2 &&
            fwu_abi_version() == can_abi_version()) ? 0 : 1;
}
