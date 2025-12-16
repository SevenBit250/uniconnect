#ifndef __USB_DEVICE_CLASS_H
#define __USB_DEVICE_CLASS_H

#ifdef __cplusplus
 extern "C" {
#endif

void cdc_acm_init(uint8_t busid, uintptr_t reg_base);

void cdc_acm_handle (void);

#ifdef __cplusplus
}
#endif

#endif /* __USB_DEVICE_CLASS_H */


