#ifndef MOCK_CH58X_H
#define MOCK_CH58X_H
#include <stdint.h>
#define __INTERRUPT
#define __HIGH_CODE
#define USB_IRQn 0
#define PFIC_EnableIRQ(x) ((void)(x))
#define PFIC_DisableIRQ(x) ((void)(x))
extern volatile uint16_t R16_PIN_ANALOG_IE;
extern volatile uint16_t R16_UEP0_DMA;
extern volatile uint16_t R16_UEP1_DMA;
extern volatile uint16_t R16_UEP2_DMA;
extern volatile uint8_t R8_UDEV_CTRL;
extern volatile uint8_t R8_UEP0_CTRL;
extern volatile uint8_t R8_UEP0_T_LEN;
extern volatile uint8_t R8_UEP1_CTRL;
extern volatile uint8_t R8_UEP1_T_LEN;
extern volatile uint8_t R8_UEP2_3_MOD;
extern volatile uint8_t R8_UEP2_CTRL;
extern volatile uint8_t R8_UEP2_T_LEN;
extern volatile uint8_t R8_UEP4_1_MOD;
extern volatile uint8_t R8_USB_CTRL;
extern volatile uint8_t R8_USB_DEV_AD;
extern volatile uint8_t R8_USB_INT_EN;
extern volatile uint8_t R8_USB_INT_FG;
extern volatile uint8_t R8_USB_INT_ST;
extern volatile uint8_t R8_USB_MIS_ST;
extern volatile uint8_t R8_USB_RX_LEN;
#define MASK_UEP_T_RES 0x03
#define MASK_UIS_ENDP 0x0F
#define MASK_UIS_TOKEN 0x30
#define RB_PIN_USB_DP_PU 0x40
#define RB_PIN_USB_IE 0x80
#define RB_UC_DEV_PU_EN 0x20
#define RB_UC_DMA_EN 0x01
#define RB_UC_INT_BUSY 0x08
#define RB_UD_PD_DIS 0x80
#define RB_UD_PORT_EN 0x01
#define RB_UEP1_TX_EN 0x40
#define RB_UEP2_TX_EN 0x04
#define RB_UEP_R_TOG 0x80
#define RB_UEP_T_TOG 0x40
#define RB_UIE_BUS_RST 0x01
#define RB_UIE_SUSPEND 0x04
#define RB_UIE_TRANSFER 0x02
#define RB_UIF_BUS_RST 0x01
#define RB_UIF_SUSPEND 0x04
#define RB_UIF_TRANSFER 0x02
#define RB_UIS_SETUP_ACT 0x80
#define RB_UIS_TOG_OK 0x40
#define RB_UMS_SUSPEND 0x04
#define UEP_R_RES_ACK 0x00
#define UEP_R_RES_NAK 0x08
#define UEP_R_RES_STALL 0x0C
#define UEP_T_RES_ACK 0x00
#define UEP_T_RES_NAK 0x02
#define UEP_T_RES_STALL 0x03
#define UIS_TOKEN_IN 0x20
#define UIS_TOKEN_OUT 0x00
#endif
