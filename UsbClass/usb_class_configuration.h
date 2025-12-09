#ifndef __USB_CLASS_CONFIGURATION_H
#define __USB_CLASS_CONFIGURATION_H

#ifdef __cplusplus
 extern "C" {
#endif

void cdc_acm_init(uint8_t busid, uintptr_t reg_base);

void cdc_acm_data_send_with_dtr_test(uint8_t busid);

#ifdef __cplusplus
}
#endif

#endif /* __USB_CLASS_CONFIGURATION_H */


