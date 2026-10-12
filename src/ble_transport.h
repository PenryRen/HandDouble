#ifndef BLE_TRANSPORT_H
#define BLE_TRANSPORT_H
#include <stdint.h>
void ble_transport_init(void);
void ble_transport_poll(uint32_t now_ms);
int ble_notify(uint16_t handle,const uint8_t *data,uint16_t length);
int ble_authorize_role(uint16_t handle,uint8_t role);
#endif
