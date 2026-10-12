#include "CH58x_common.h"
#include "usb_hid.h"
#include "hd.h"
#include <string.h>
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
void *memcpy(void *d,const void *s,size_t n) {uint8_t *a=d;const uint8_t *b=s;while(n--) *a++=*b++;return d;}
void *memset(void *d,int v,size_t n) {uint8_t *a=d;while(n--) *a++=(uint8_t)v;return d;}
uint16_t hd_le16(const uint8_t *p) {return p[0]|((uint16_t)p[1]<<8);}
void hd_put16(uint8_t *p,uint16_t v) {p[0]=v;p[1]=v>>8;}
static unsigned completions;static uint8_t result;
void hd_hid_done(uint8_t s) {++completions;result=s;}
extern void USB_IRQHandler(void);
static uint32_t clock;
static uint8_t *dma(uint16_t v) {return (uint8_t *)(0x20000000u|v);}
static void setup_req(uint8_t type,uint8_t req,uint16_t value,uint16_t index,uint16_t length) {
    uint8_t *p=dma(R16_UEP0_DMA);p[0]=type;p[1]=req;hd_put16(p+2,value);hd_put16(p+4,index);hd_put16(p+6,length);
    R8_USB_INT_ST=RB_UIS_SETUP_ACT;R8_USB_INT_FG=RB_UIF_TRANSFER;USB_IRQHandler();
}
static void in(uint8_t e) {R8_USB_INT_ST=UIS_TOKEN_IN|e;R8_USB_INT_FG=RB_UIF_TRANSFER;USB_IRQHandler();}
static void drain(void) {
    unsigned i;
    for(i=0;i<600;i++) {
        usb_hid_poll(++clock);
        if((R8_UEP1_CTRL&MASK_UEP_T_RES)==UEP_T_RES_ACK) in(1);
        if((R8_UEP2_CTRL&MASK_UEP_T_RES)==UEP_T_RES_ACK) in(2);
    }
}
int test_main(void) {
    uint8_t p[4],*m,*k;int32_t sum_x=0,sum_y=0;unsigned count;
    usb_hid_init();CHECK(!usb_hid_ready());m=dma(R16_UEP2_DMA);k=dma(R16_UEP1_DMA);
    setup_req(0x80,6,0x100,0,18);CHECK(R8_UEP0_T_LEN==18 && dma(R16_UEP0_DMA)[7]==64);in(0);
    setup_req(0x80,6,0x200,0,255);CHECK(R8_UEP0_T_LEN==59 && dma(R16_UEP0_DMA)[4]==2);in(0);
    setup_req(0x81,6,0x2200,0,255);CHECK(dma(R16_UEP0_DMA)[0]==5 && dma(R16_UEP0_DMA)[1]==1 && dma(R16_UEP0_DMA)[3]==6);in(0);
    setup_req(0x81,6,0x2200,2,255);CHECK((R8_UEP0_CTRL&MASK_UEP_T_RES)==UEP_T_RES_STALL);
    setup_req(0x81,6,0x2200,1,255);CHECK(dma(R16_UEP0_DMA)[0]==5);
    setup_req(0,5,42,0,0);CHECK(R8_USB_DEV_AD==0);in(0);CHECK(R8_USB_DEV_AD==42);
    setup_req(0,9,1,0,0);in(0);drain();CHECK(usb_hid_ready());
    /* Application middle/right numbering converts to HID middle/right bits. */
    p[0]=2;CHECK(usb_hid_start(3,p)==0);usb_hid_poll(++clock);CHECK(m[0]==4);in(2);usb_hid_poll(++clock);CHECK(result==0);
    p[0]=3;CHECK(usb_hid_start(3,p)==0);usb_hid_poll(++clock);CHECK(m[0]==6);in(2);usb_hid_poll(++clock);
    CHECK(usb_hid_start(8,p)==0);drain();CHECK(k[0]==0 && m[0]==0);
    /* Signed int16 movement including -32768 is split exactly into [-127,127]. */
    hd_put16(p,32767);hd_put16(p+2,0x8000);CHECK(usb_hid_start(1,p)==0);count=completions;
    while(completions==count && clock<5000) {
        usb_hid_poll(++clock);
        if((R8_UEP2_CTRL&MASK_UEP_T_RES)==UEP_T_RES_ACK) {sum_x+=(int8_t)m[1];sum_y+=(int8_t)m[2];in(2);}
    }
    CHECK(result==0 && sum_x==32767 && sum_y==-32768);
    /* Click is a press followed by release, with ACK only after both INs. */
    count=completions;CHECK(usb_hid_start(2,p)==0);usb_hid_poll(++clock);CHECK(m[0]==1 && completions==count);
    in(2);usb_hid_poll(++clock);CHECK(m[0]==0 && completions==count);in(2);usb_hid_poll(++clock);CHECK(completions==count+1);
    /* Modifier and six-key rollover without overwriting existing keys. */
    p[0]=0xe1;CHECK(usb_hid_start(6,p)==0);drain();CHECK(k[0]==2);
    for(unsigned i=4;i<10;i++) {p[0]=i;CHECK(usb_hid_start(6,p)==0);drain();}
    p[0]=10;CHECK(usb_hid_start(6,p)==HD_BUSY);p[0]=5;CHECK(usb_hid_start(7,p)==0);drain();
    p[0]=10;CHECK(usb_hid_start(6,p)==0);drain();
    usb_hid_release();drain();for(unsigned i=0;i<8;i++) CHECK(k[i]==0);
    /* Mouse boot protocol is 3 bytes; report protocol includes both wheels. */
    setup_req(0x21,11,0,1,0);in(0);CHECK(usb_hid_start(3,(uint8_t[]){1})==0);usb_hid_poll(++clock);CHECK(R8_UEP2_T_LEN==3);in(2);usb_hid_poll(++clock);
    setup_req(0x21,11,1,1,0);in(0);hd_put16(p,2);hd_put16(p+2,(uint16_t)-3);
    CHECK(usb_hid_start(5,p)==0);usb_hid_poll(++clock);CHECK(R8_UEP2_T_LEN==5 && (int8_t)m[3]==2 && (int8_t)m[4]==-3);in(2);usb_hid_poll(++clock);
    /* Enumeration reset must not resurrect a held mouse button or keyboard key. */
    R8_USB_INT_FG=RB_UIF_BUS_RST;USB_IRQHandler();usb_hid_poll(++clock);CHECK(!usb_hid_ready());
    setup_req(0,9,1,0,0);in(0);drain();CHECK(m[0]==0);
    CHECK(usb_hid_start(2,p)==0);usb_hid_poll(++clock);usb_hid_poll(clock+=901);CHECK(result==HD_TIMEOUT);drain();CHECK(m[0]==0);
    return 0;
}
