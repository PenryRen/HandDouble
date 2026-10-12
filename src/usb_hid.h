#ifndef USB_HID_H
#define USB_HID_H
#include <stdint.h>
void usb_hid_init(void);
void usb_hid_poll(uint32_t now);
int usb_hid_ready(void);
uint8_t usb_hid_start(uint8_t command,const uint8_t *parameters);
void usb_hid_release(void);
#endif
