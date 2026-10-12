#include "CH58x_common.h"
#include "usb_hid.h"
#include "hd.h"
#include <string.h>

/* Interface 0: boot keyboard; interface 1: five-button relative mouse,
 * vertical wheel and Consumer AC Pan. No report IDs. */
static const uint8_t keyboard_desc[]={
  0x05,1,0x09,6,0xa1,1,0x05,7,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,
  0x75,1,0x95,8,0x81,2,0x95,1,0x75,8,0x81,1,
  0x95,5,0x75,1,0x05,8,0x19,1,0x29,5,0x91,2,0x95,1,0x75,3,0x91,1,
  0x95,6,0x75,8,0x15,0,0x26,0xff,0,0x05,7,0x19,0,0x29,0xff,0x81,0,0xc0};
static const uint8_t mouse_desc[]={
  0x05,1,0x09,2,0xa1,1,0x09,1,0xa1,0,0x05,9,0x19,1,0x29,5,
  0x15,0,0x25,1,0x75,1,0x95,5,0x81,2,0x75,3,0x95,1,0x81,1,
  0x05,1,0x09,0x30,0x09,0x31,0x09,0x38,0x15,0x81,0x25,0x7f,
  0x75,8,0x95,3,0x81,6,0x05,0x0c,0x0a,0x38,2,0x95,1,0x81,6,0xc0,0xc0};
/* WCH example VID/PID for development only; obtain assigned IDs for production. */
static const uint8_t device_desc[]={18,1,0x10,1,0,0,0,64,0x3d,0x41,7,0x21,0,1,1,2,0,1};
static const uint8_t config_desc[]={
  9,2,59,0,2,1,0,0x80,50,
  9,4,0,0,1,3,1,1,0,
  9,0x21,0x11,1,0,1,0x22,sizeof(keyboard_desc),0,
  7,5,0x81,3,8,0,1,
  9,4,1,0,1,3,1,2,0,
  9,0x21,0x11,1,0,1,0x22,sizeof(mouse_desc),0,
  7,5,0x82,3,5,0,1};
static const uint8_t lang_desc[]={4,3,9,4};
static const uint8_t manufacturer[]={8,3,'W',0,'C',0,'H',0};
static const uint8_t product[]={22,3,'H',0,'a',0,'n',0,'d',0,'D',0,'o',0,'u',0,'b',0,'l',0,'e',0};
static __attribute__((aligned(4))) uint8_t ep0[64],ep1[64],ep2[64];
static volatile uint8_t configured,suspended,busy[2],halted[2];
static volatile uint16_t completed[2];
static volatile uint8_t reset_seen;
static uint8_t keyboard[8],buttons,leds,protocol[2]={1,1},idle[2];
static uint32_t last_sent[2];
static uint8_t control_out,control_address,address_pending,control_zlp;
static const uint8_t *control_ptr;
static uint16_t control_left;
static uint8_t control_value[8];
static struct {
    uint8_t active,command,stage,waiting,endpoint,release_pending;
    int32_t x,y,v,h;
    uint16_t ticket;
    uint32_t started;
} job;
static uint32_t clock_ms;
static uint8_t was_ready;

static void ep0_next(void) {
    uint8_t n=control_left>64?64:(uint8_t)control_left;
    if(n) memcpy(ep0,control_ptr,n);
    control_ptr+=n; control_left-=n; R8_UEP0_T_LEN=n;
}
static void setup(void) {
    uint8_t type=ep0[0],req=ep0[1],recipient=type&31,fail=0;
    uint16_t value=hd_le16(ep0+2),index=hd_le16(ep0+4),length=hd_le16(ep0+6),size=0;
    const uint8_t *data=control_value;
    control_out=0; control_left=0; address_pending=0;control_zlp=0;
    memset(control_value,0,sizeof(control_value));
    if((type&0x60)==0) {
        switch(req) {
        case 6: /* GET_DESCRIPTOR */
            if(!(type&0x80)) { fail=1; break; }
            switch(value>>8) {
            case 1: if(recipient || (value&255)) fail=1; else { data=device_desc; size=sizeof(device_desc); } break;
            case 2: if(recipient || (value&255)) fail=1; else { data=config_desc; size=sizeof(config_desc); } break;
            case 3:
                if(recipient) fail=1;
                else if((value&255)==0) {data=lang_desc;size=sizeof(lang_desc);}
                else if((value&255)==1) {data=manufacturer;size=sizeof(manufacturer);}
                else if((value&255)==2) {data=product;size=sizeof(product);}
                else fail=1;
                break;
            case 0x21: if(recipient!=1 || index>1) fail=1; else {data=config_desc+(index?43:18);size=9;} break;
            case 0x22: if(recipient!=1 || index>1) fail=1; else {data=index?mouse_desc:keyboard_desc;size=index?sizeof(mouse_desc):sizeof(keyboard_desc);} break;
            default: fail=1;
            } break;
        case 5: if(type || index || length || value>127) fail=1; else {control_address=(uint8_t)value;address_pending=1;} break;
        case 9: if(type || index || length || value>1) fail=1; else {configured=(uint8_t)value;reset_seen=1; busy[0]=busy[1]=0; halted[0]=halted[1]=0;
                R8_UEP1_CTRL=UEP_R_RES_NAK|UEP_T_RES_NAK; R8_UEP2_CTRL=UEP_R_RES_NAK|UEP_T_RES_NAK;} break;
        case 8: if(type!=0x80 || value || index || length!=1) fail=1; else {control_value[0]=configured;size=1;} break;
        case 0:
            if(!(type&0x80) || value || length!=2) fail=1;
            else if(recipient==1 && index>1) fail=1;
            else if(recipient==2) {
                if(index==0x81 || index==0x82) control_value[0]=halted[(index&15)-1];
                else if(index!=0 && index!=0x80) fail=1;
            } else if(recipient>2 || (recipient==0 && index)) fail=1;
            size=2; break;
        case 1: case 3: /* endpoint halt only, remote wake not advertised */
            if(type!=2 || value || length || (index!=0x81 && index!=0x82)) fail=1;
            else {
                uint8_t e=(uint8_t)((index&15)-1); halted[e]=(req==3); busy[e]=0;
                if(e==0) R8_UEP1_CTRL=UEP_R_RES_NAK|(req==3?UEP_T_RES_STALL:UEP_T_RES_NAK);
                else R8_UEP2_CTRL=UEP_R_RES_NAK|(req==3?UEP_T_RES_STALL:UEP_T_RES_NAK);
            } break;
        case 10: if(type!=0x81 || index>1 || value || length!=1) fail=1; else size=1; break;
        case 11: if(type!=1 || index>1 || value || length) fail=1; break;
        default: fail=1;
        }
    } else if((type&0x7f)==0x21 && index<2 && configured) {
        switch(req) {
        case 1: /* GET_REPORT: input or keyboard LED output */
            if(!(type&0x80) || (value&255)) fail=1;
            else if((value>>8)==1) {
                if(index==0) memcpy(control_value,keyboard,8);
                else control_value[0]=buttons;
                size=index?(protocol[1]?5:3):8;
            } else if((value>>8)==2 && index==0) {control_value[0]=leds;size=1;} else fail=1;
            break;
        case 2: if(type!=0xa1 || (value&255) || length!=1) fail=1; else {control_value[0]=idle[index];size=1;} break;
        case 3: if(type!=0xa1 || value || length!=1) fail=1; else {control_value[0]=protocol[index];size=1;} break;
        case 9: if(type!=0x21 || index || value!=0x200 || length!=1) fail=1; else control_out=1; break;
        case 10: if(type!=0x21 || (value&255) || length) fail=1; else idle[index]=(uint8_t)(value>>8); break;
        case 11: if(type!=0x21 || value>1 || length) fail=1; else protocol[index]=(uint8_t)value; break;
        default: fail=1;
        }
    } else fail=1;
    if(fail) { R8_UEP0_T_LEN=0; R8_UEP0_CTRL=RB_UEP_R_TOG|RB_UEP_T_TOG|UEP_R_RES_STALL|UEP_T_RES_STALL; return; }
    control_ptr=data; control_left=size<length?size:length;
    control_zlp=(size && size<length && (size%64)==0);
    ep0_next(); R8_UEP0_CTRL=RB_UEP_R_TOG|RB_UEP_T_TOG|UEP_R_RES_ACK|(control_out?UEP_T_RES_NAK:UEP_T_RES_ACK);
}
__INTERRUPT __HIGH_CODE void USB_IRQHandler(void) {
    uint8_t flags=R8_USB_INT_FG;
    if(flags&RB_UIF_BUS_RST) {
        configured=suspended=0; busy[0]=busy[1]=0; halted[0]=halted[1]=0;
        reset_seen=1;
        protocol[0]=protocol[1]=1; idle[0]=idle[1]=0; address_pending=control_out=0;
        control_left=0; R8_USB_DEV_AD=0;
        R8_UEP0_CTRL=UEP_R_RES_ACK|UEP_T_RES_NAK;
        R8_UEP1_CTRL=R8_UEP2_CTRL=UEP_R_RES_NAK|UEP_T_RES_NAK;
        R8_USB_INT_FG=RB_UIF_BUS_RST;
    } else if(flags&RB_UIF_TRANSFER) {
        uint8_t token=R8_USB_INT_ST&(MASK_UIS_TOKEN|MASK_UIS_ENDP);
        if(R8_USB_INT_ST&RB_UIS_SETUP_ACT) setup();
        else if(token==UIS_TOKEN_IN) {
            if(address_pending) {R8_USB_DEV_AD=control_address;address_pending=0;}
            if(control_left || control_zlp) {
                if(!control_left) control_zlp=0;
                ep0_next();R8_UEP0_CTRL^=RB_UEP_T_TOG;
            } else {R8_UEP0_T_LEN=0;R8_UEP0_CTRL=RB_UEP_R_TOG|UEP_R_RES_ACK|UEP_T_RES_NAK;}
        } else if(token==UIS_TOKEN_OUT && (R8_USB_INT_ST&RB_UIS_TOG_OK)) {
            if(control_out && R8_USB_RX_LEN==1) { leds=ep0[0];control_out=0;R8_UEP0_T_LEN=0;R8_UEP0_CTRL=RB_UEP_T_TOG|UEP_R_RES_ACK|UEP_T_RES_ACK; }
        } else if(token==(UIS_TOKEN_IN|1)) {
            R8_UEP1_CTRL^=RB_UEP_T_TOG; R8_UEP1_CTRL=(R8_UEP1_CTRL&~MASK_UEP_T_RES)|UEP_T_RES_NAK;
            busy[0]=0; ++completed[0];
        } else if(token==(UIS_TOKEN_IN|2)) {
            R8_UEP2_CTRL^=RB_UEP_T_TOG; R8_UEP2_CTRL=(R8_UEP2_CTRL&~MASK_UEP_T_RES)|UEP_T_RES_NAK;
            busy[1]=0; ++completed[1];
        }
        R8_USB_INT_FG=RB_UIF_TRANSFER;
    }
    if(flags&RB_UIF_SUSPEND) { suspended=!!(R8_USB_MIS_ST&RB_UMS_SUSPEND);R8_USB_INT_FG=RB_UIF_SUSPEND; }
}
void usb_hid_init(void) {
    R8_USB_CTRL=0; R8_UEP4_1_MOD=RB_UEP1_TX_EN; R8_UEP2_3_MOD=RB_UEP2_TX_EN;
    R16_UEP0_DMA=(uint16_t)(uint32_t)ep0;R16_UEP1_DMA=(uint16_t)(uint32_t)ep1;R16_UEP2_DMA=(uint16_t)(uint32_t)ep2;
    R8_UEP0_CTRL=UEP_R_RES_ACK|UEP_T_RES_NAK;
    R8_UEP1_CTRL=R8_UEP2_CTRL=UEP_R_RES_NAK|UEP_T_RES_NAK;
    R8_USB_DEV_AD=0;R8_USB_INT_FG=0xff;
    R8_USB_CTRL=RB_UC_DEV_PU_EN|RB_UC_INT_BUSY|RB_UC_DMA_EN;
    R16_PIN_ANALOG_IE|=RB_PIN_USB_IE|RB_PIN_USB_DP_PU;
    R8_UDEV_CTRL=RB_UD_PD_DIS|RB_UD_PORT_EN;
    R8_USB_INT_EN=RB_UIE_SUSPEND|RB_UIE_BUS_RST|RB_UIE_TRANSFER;
    PFIC_EnableIRQ(USB_IRQn);
    usb_hid_release();
}
int usb_hid_ready(void) { return configured && !suspended; }
static int send_report(uint8_t e,const uint8_t *p,uint8_t n) {
    if(!usb_hid_ready() || busy[e] || halted[e]) return 0;
    PFIC_DisableIRQ(USB_IRQn);
    memcpy(e?ep2:ep1,p,n); busy[e]=1;
    job.ticket=(uint16_t)(completed[e]+1); job.endpoint=e;
    if(e) {R8_UEP2_T_LEN=n;R8_UEP2_CTRL=(R8_UEP2_CTRL&~MASK_UEP_T_RES)|UEP_T_RES_ACK;}
    else {R8_UEP1_T_LEN=n;R8_UEP1_CTRL=(R8_UEP1_CTRL&~MASK_UEP_T_RES)|UEP_T_RES_ACK;}
    PFIC_EnableIRQ(USB_IRQn);last_sent[e]=clock_ms;return 1;
}
void usb_hid_release(void) {
    PFIC_DisableIRQ(USB_IRQn);
    R8_UEP1_CTRL=(R8_UEP1_CTRL&~MASK_UEP_T_RES)|UEP_T_RES_NAK;
    R8_UEP2_CTRL=(R8_UEP2_CTRL&~MASK_UEP_T_RES)|UEP_T_RES_NAK;
    busy[0]=busy[1]=0; memset(keyboard,0,8);buttons=0;
    memset(&job,0,sizeof(job));job.release_pending=3;
    PFIC_EnableIRQ(USB_IRQn);
}
static uint8_t button_bit(uint8_t b) { static const uint8_t bits[]={0,1,4,2,8,16};return b<=5?bits[b]:0; }
uint8_t usb_hid_start(uint8_t cmd,const uint8_t *p) {
    unsigned i;
    if(job.active || job.release_pending) return HD_BUSY;
    if(!usb_hid_ready()) return HD_NO_HID;
    if(cmd==3 || cmd==4) {
        uint8_t bit=button_bit(p[0]);if(!bit) return HD_PARAM;
        if(cmd==3) buttons|=bit;else buttons&=(uint8_t)~bit;
    } else if(cmd==6 || cmd==7) {
        uint8_t k=p[0];
        if(k>=0xe0 && k<=0xe7) {if(cmd==6) keyboard[0]|=(1u<<(k-0xe0));else keyboard[0]&=(uint8_t)~(1u<<(k-0xe0));}
        else {
            if(k<4) return HD_PARAM;
            for(i=2;i<8;i++) if(keyboard[i]==k) break;
            if(cmd==7) {if(i<8) keyboard[i]=0;}
            else if(i==8) {for(i=2;i<8;i++) if(!keyboard[i]) break;if(i==8) return HD_BUSY;keyboard[i]=k;}
        }
    } else if(cmd==2 && (buttons&1)) return HD_BUSY;
    else if(cmd==8) {memset(keyboard,0,8);buttons=0;}
    memset(&job,0,sizeof(job));job.active=1;job.command=cmd;job.started=clock_ms;
    if(cmd==1) {job.x=(int16_t)hd_le16(p);job.y=(int16_t)hd_le16(p+2);}
    if(cmd==5) {job.v=(int16_t)hd_le16(p);job.h=(int16_t)hd_le16(p+2);}
    return HD_OK;
}
static int8_t step(int32_t *v) {int32_t s=*v>127?127:*v< -127?-127:*v;*v-=s;return (int8_t)s;}
void usb_hid_poll(uint32_t now_ms) {
    uint8_t m[5]={buttons,0,0,0,0},e=1,n=protocol[1]?5:3;
    clock_ms=now_ms;
    if(reset_seen) {reset_seen=0;usb_hid_release();hd_hid_done(HD_NO_HID);}
    m[0]=buttons;
    if(!usb_hid_ready()) {
        if(job.active || was_ready) {usb_hid_release();hd_hid_done(HD_NO_HID);}
        was_ready=0;
        return;
    }
    was_ready=1;
    if(job.release_pending) {
        /* Send urgent zero reports before accepting new remote operations. */
        if((job.release_pending&1) && !busy[0] && send_report(0,keyboard,8)) job.release_pending&=~1;
        if((job.release_pending&2) && !busy[1] && send_report(1,m,n)) job.release_pending&=~2;
        return;
    }
    if(!job.active) {
        if(idle[0] && now_ms-last_sent[0]>=idle[0]*4u) send_report(0,keyboard,8);
        if(idle[1] && now_ms-last_sent[1]>=idle[1]*4u) send_report(1,m,n);
        return;
    }
    if(now_ms-job.started>=900) {usb_hid_release();hd_hid_done(HD_TIMEOUT);return;}
    if(job.waiting) {
        if(completed[job.endpoint]!=job.ticket) return;
        job.waiting=0;
        if(job.command==1 || job.command==5) {
            if(!job.x && !job.y && !job.v && !job.h) {job.active=0;hd_hid_done(HD_OK);return;}
        } else if((job.command!=2 && job.command!=8) || job.stage>=2) {job.active=0;hd_hid_done(HD_OK);return;}
    }
    if(job.command==6 || job.command==7 || (job.command==8 && job.stage==0)) e=0;
    if(busy[e] || halted[e]) return;
    if(job.command==1) {m[1]=(uint8_t)step(&job.x);m[2]=(uint8_t)step(&job.y);}
    else if(job.command==5) {if(!protocol[1]) {job.active=0;hd_hid_done(HD_FAILED);return;}m[3]=(uint8_t)step(&job.v);m[4]=(uint8_t)step(&job.h);}
    else if(job.command==2) m[0]=job.stage==0?(buttons|1):buttons;
    if(send_report(e,e?m:keyboard,e?n:8)) {job.waiting=1;++job.stage;}
}
