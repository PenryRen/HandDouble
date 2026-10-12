#ifndef HD_H
#define HD_H
#include <stdint.h>
#include <stddef.h>
#define HD_MAX_PACKET 244
#define HD_CONNECTIONS 2
#define HD_INVALID 0xffff
#define HD_HEARTBEAT_MS 5000u
#define HD_CAPTURE_MS 2000u
#define HD_FRAGMENT_MS 8000u /* original attempt plus three phone retries */
enum { HD_OK, HD_UNKNOWN, HD_PARAM, HD_NO_HID, HD_BUSY, HD_FAILED,
       HD_NO_TARGET, HD_TIMEOUT, HD_DENIED };
enum { HD_PC=1, HD_PHONE=2 };
/* All callbacks run in the cooperative main loop, never in the USB ISR. */
typedef struct {
    int (*notify)(uint16_t handle, const uint8_t *data, uint16_t length);
    int (*authorize_role)(uint16_t handle, uint8_t role);
    uint8_t (*hid_start)(uint8_t command, const uint8_t *parameters);
    void (*hid_release)(void);
    int (*hid_ready)(void);
} hd_ops;
void hd_init(const hd_ops *ops);
int hd_connect(uint16_t handle);
void hd_disconnect(uint16_t handle);
void hd_security(uint16_t handle, int secure);
void hd_subscribe(uint16_t handle, int enabled);
void hd_mtu(uint16_t handle, uint16_t mtu);
uint16_t hd_link_mtu(uint16_t handle);
int hd_receive(uint16_t handle, const uint8_t *data, uint16_t length);
void hd_poll(uint32_t now_ms);
void hd_hid_done(uint8_t status);
uint16_t hd_le16(const uint8_t *p);
uint32_t hd_le32(const uint8_t *p);
void hd_put16(uint8_t *p, uint16_t v);
void hd_put32(uint8_t *p, uint32_t v);
#endif
