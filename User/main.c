#include "main.h"
#include "debug.h"
#include <rtthread.h>
#include "usbd_core.h"

static system_status_t sysyem_status = SYS_STA_USB_NOT_CONNECTED;

int main(void)
{
	while(1) {
		rt_thread_mdelay(10000);
	}
}

void usb_dc_low_level_init(void)
{
    RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_USBPHY);
    RCC_USBHSPLLCLKConfig(RCC_HSBHSPLLCLKSource_HSE);
    RCC_USBHSConfig(RCC_USBPLL_Div2);
    RCC_USBHSPLLCKREFCLKConfig( RCC_USBHSPLLCKREFCLK_4M);
    RCC_USBHSPHYPLLALIVEcmd(ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, ENABLE);
    NVIC_EnableIRQ(USBHS_IRQn);
}

void updateSystemStatus(system_status_t sta)
{
    sysyem_status = sta;
}

system_status_t getSystemStatus(void)
{
    return sysyem_status;
}

