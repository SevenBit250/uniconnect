#include "main.h"
#include "debug.h"
#include <rtthread.h>
#include "usbd_core.h"

static system_status_t sysyem_status = SYS_STA_USB_NOT_CONNECTED;
led_open_t g_led_open = {0};

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

