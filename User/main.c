#include "main.h"
#include "debug.h"
#include <rtthread.h>
#include "usbd_core.h"
#include "boot_shared.h"

static system_status_t sysyem_status = SYS_STA_USB_NOT_CONNECTED;
led_open_t g_led_open = {0};

/* APP镜像头魔法字: .appsig段被Link.ld置于FLASH(0x5000)首4B, boot侧
 * app_sig_ok校验后跳APP_BASE+4(=启动入口_start) */
const uint32_t app_sig __attribute__((section(".appsig"), used)) = APP_SIG_MAGIC;

int main(void)
{
	while(1) {
		rt_thread_mdelay(10000);
	}
}

void updateSystemStatus(system_status_t sta)
{
    sysyem_status = sta;
}

system_status_t getSystemStatus(void)
{
    return sysyem_status;
}

