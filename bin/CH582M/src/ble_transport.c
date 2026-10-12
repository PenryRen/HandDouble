#include "CONFIG.h"
#include "ble_transport.h"
#include "hd.h"
#include <string.h>

/* UUID octets are little endian on the BLE link. */
static const uint8_t service_uuid[16]={0xd9,0x63,0xbf,0xff,0x9f,0x6d,0x94,0x85,0x84,0x4e,0xfd,0xac,0x7f,0xa7,0x3b,0x9a};
static const uint8_t rx_uuid[16]={0xd9,0x63,0xbf,0xff,0x9f,0x6d,0x94,0x85,0x84,0x4e,0xfd,0xac,0x80,0xa7,0x3b,0x9a};
static const uint8_t tx_uuid[16]={0xd9,0x63,0xbf,0xff,0x9f,0x6d,0x94,0x85,0x84,0x4e,0xfd,0xac,0x81,0xa7,0x3b,0x9a};
static const gattAttrType_t service={16,service_uuid};
static uint8_t rx_props=GATT_PROP_WRITE,tx_props=GATT_PROP_NOTIFY,dummy;
static gattCharCfg_t subscriptions[HD_CONNECTIONS];
static gattAttribute_t attrs[]={
    {{2,primaryServiceUUID},GATT_PERMIT_READ,0,(uint8_t *)&service},
    {{2,characterUUID},GATT_PERMIT_READ,0,&rx_props},
    {{16,rx_uuid},GATT_PERMIT_WRITE|GATT_PERMIT_ENCRYPT_WRITE,0,&dummy},
    {{2,characterUUID},GATT_PERMIT_READ,0,&tx_props},
    {{16,tx_uuid},0,0,&dummy},
    {{2,clientCharCfgUUID},GATT_PERMIT_READ|GATT_PERMIT_WRITE|GATT_PERMIT_ENCRYPT_WRITE,0,(uint8_t *)subscriptions}
};
static uint8_t task_id;
static uint16_t handles[HD_CONNECTIONS]={HD_INVALID,HD_INVALID};
static uint32_t clock_ms,pair_deadline,button_since;
static uint8_t pairing_open,button_down,button_consumed;

/* Bind roles to a persisted bond LTK rather than a rotating radio address.
 * Dedicated data-flash erase block. SDK bond storage is 0x7e00..0x7fff. */
#define TRUST_ADDR 0x7c00u
#define TRUST_MAGIC 0x31444842u
typedef struct { uint32_t magic; uint8_t key[2][16],valid[2],pad[2]; uint32_t checksum; } trust_record;
static __attribute__((aligned(4))) trust_record trusted;
static uint16_t inspect_handle;
static uint8_t inspect_key[16],inspect_valid;
static uint32_t checksum(const trust_record *r) {
    const uint8_t *p=(const uint8_t *)r;unsigned i;uint32_t v=2166136261u;
    for(i=0;i<offsetof(trust_record,checksum);i++) v=(v^p[i])*16777619u;
    return v;
}
static void inspect_link(linkDBItem_t *p) {
    if(p->connectionHandle!=inspect_handle) return;
    if((p->stateFlags&(LINK_CONNECTED|LINK_ENCRYPTED|LINK_BOUND))!=(LINK_CONNECTED|LINK_ENCRYPTED|LINK_BOUND)) return;
    if(!p->pEncParams || p->pEncParams->keySize<16) return;
    memcpy(inspect_key,p->pEncParams->ltk,16);inspect_valid=1;
}
static int bond_key(uint16_t handle) {
    inspect_handle=handle;inspect_valid=0;linkDB_PerformFunc(inspect_link);return inspect_valid;
}
int ble_authorize_role(uint16_t handle,uint8_t role) {
    unsigned slot=role-1,other=slot^1;
    trust_record next;
    if(role<1 || role>2 || !bond_key(handle)) return 0;
    if(trusted.valid[slot] && !memcmp(trusted.key[slot],inspect_key,16)) return 1;
    if(!pairing_open || (trusted.valid[other] && !memcmp(trusted.key[other],inspect_key,16))) return 0;
    next=trusted;next.magic=TRUST_MAGIC;next.valid[slot]=1;
    memcpy(next.key[slot],inspect_key,16);next.checksum=checksum(&next);
    if(EEPROM_ERASE(TRUST_ADDR,512) || EEPROM_WRITE(TRUST_ADDR,&next,sizeof(next))) return 0;
    trusted=next;return 1;
}
static int known_key(void) {
    unsigned i;
    for(i=0;i<2;i++) if(trusted.valid[i] && !memcmp(trusted.key[i],inspect_key,16)) return 1;
    return 0;
}
static bStatus_t read_attr(uint16_t handle,gattAttribute_t *attr,uint8_t *value,
                          uint16_t *length,uint16_t offset,uint16_t max,uint8_t method) {
    uint16_t cfg;
    (void)method;
    if(attr!=&attrs[5]) return ATT_ERR_ATTR_NOT_FOUND;
    if(offset) return ATT_ERR_ATTR_NOT_LONG;
    if(max<2) return ATT_ERR_INVALID_VALUE_SIZE;
    cfg=GATTServApp_ReadCharCfg(handle,subscriptions);hd_put16(value,cfg);*length=2;return SUCCESS;
}
static bStatus_t write_attr(uint16_t handle,gattAttribute_t *attr,uint8_t *value,
                           uint16_t length,uint16_t offset,uint8_t method) {
    bStatus_t status;
    int secure=bond_key(handle) && (pairing_open || known_key());
    hd_security(handle,secure);
    if(!secure) return ATT_ERR_INSUFFICIENT_AUTHOR;
    if(method!=ATT_WRITE_REQ) return ATT_ERR_UNSUPPORTED_REQ;
    if(offset) return ATT_ERR_ATTR_NOT_LONG;
    if(attr==&attrs[5]) {
        status=GATTServApp_ProcessCCCWriteReq(handle,attr,value,length,offset,GATT_CLIENT_CFG_NOTIFY);
        if(status==SUCCESS) hd_subscribe(handle,GATTServApp_ReadCharCfg(handle,subscriptions)==GATT_CLIENT_CFG_NOTIFY);
        return status;
    }
    if(attr!=&attrs[2]) return ATT_ERR_ATTR_NOT_FOUND;
    if(length<6 || length>hd_link_mtu(handle)-3 || length>HD_MAX_PACKET) return ATT_ERR_INVALID_VALUE_SIZE;
    return hd_receive(handle,value,length)?SUCCESS:ATT_ERR_INSUFFICIENT_RESOURCES;
}
static gattServiceCBs_t service_cbs={read_attr,write_attr,NULL};
int ble_notify(uint16_t handle,const uint8_t *data,uint16_t length) {
    attHandleValueNoti_t noti;
    if(!bond_key(handle) || (!pairing_open && !known_key()) || length>hd_link_mtu(handle)-3 ||
       GATTServApp_ReadCharCfg(handle,subscriptions)!=GATT_CLIENT_CFG_NOTIFY) return 0;
    noti.handle=attrs[4].handle;noti.len=length;
    noti.pValue=GATT_bm_alloc(handle,ATT_HANDLE_VALUE_NOTI,length,NULL,0);
    if(!noti.pValue) return 0;
    memcpy(noti.pValue,data,length);
    if(GATT_Notification(handle,&noti,FALSE)!=SUCCESS) {GATT_bm_free((gattMsg_t *)&noti,ATT_HANDLE_VALUE_NOTI);return 0;}
    return 1;
}
static void advertising(void) {
    uint8_t enable=(handles[0]==HD_INVALID || handles[1]==HD_INVALID);
    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED,sizeof(enable),&enable);
}
static void state_changed(gapRole_States_t state,gapRoleEvent_t *event) {
    unsigned i;
    (void)state;
    if(!event) return;
    if(event->gap.opcode==GAP_LINK_ESTABLISHED_EVENT && event->gap.hdr.status==SUCCESS) {
        uint16_t h=event->linkCmpl.connectionHandle;
        for(i=0;i<HD_CONNECTIONS;i++) if(handles[i]==HD_INVALID) break;
        if(i==HD_CONNECTIONS || !hd_connect(h)) {GAPRole_TerminateLink(h);return;}
        handles[i]=h;GATTServApp_InitCharCfg(h,subscriptions);
        GAPBondMgr_PeriSecurityReq(h);advertising();
    } else if(event->gap.opcode==GAP_LINK_TERMINATED_EVENT) {
        uint16_t h=event->linkTerminate.connectionHandle;
        for(i=0;i<HD_CONNECTIONS;i++) if(handles[i]==h) handles[i]=HD_INVALID;
        hd_disconnect(h);GATTServApp_InitCharCfg(h,subscriptions);advertising();
    }
}
static void pair_state(uint16_t handle,uint8_t state,uint8_t status) {
    if(state==GAPBOND_PAIRING_STATE_STARTED && !pairing_open) {GAPRole_TerminateLink(handle);return;}
    if(status!=SUCCESS && state!=GAPBOND_PAIRING_STATE_STARTED) GAPRole_TerminateLink(handle);
}
static gapRolesCBs_t role_cbs={state_changed,NULL,NULL};
static gapBondCBs_t bond_cbs={NULL,pair_state,NULL};
static uint16_t events(uint8_t id,uint16_t mask) {
    if(mask&SYS_EVENT_MSG) {
        uint8_t *p;
        while((p=tmos_msg_receive(id))!=NULL) {
            if(((tmos_event_hdr_t *)p)->event==GATT_MSG_EVENT) {
                gattMsgEvent_t *m=(gattMsgEvent_t *)p;
                if(m->method==ATT_MTU_UPDATED_EVENT) hd_mtu(m->connHandle,m->msg.mtuEvt.MTU);
                GATT_bm_free(&m->msg,m->method);
            }
            tmos_msg_deallocate(p);
        }
        mask^=SYS_EVENT_MSG;
    }
    if(mask&1) {GAPRole_PeripheralStartDevice(task_id,&bond_cbs,&role_cbs);mask^=1;}
    return mask;
}
void ble_transport_init(void) {
    uint8_t adv[21]={2,GAP_ADTYPE_FLAGS,GAP_ADTYPE_FLAGS_GENERAL|GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
                    17,GAP_ADTYPE_128BIT_COMPLETE};
    uint8_t scan[]={11,GAP_ADTYPE_LOCAL_NAME_COMPLETE,'C','H','5','8','2','M','-','H','I','D'};
    uint8_t enable=1,mode=GAPBOND_PAIRING_MODE_NO_PAIRING,yes=1,no=0,cap=GAPBOND_IO_CAP_NO_INPUT_NO_OUTPUT;
    uint16_t interval=240,min=12,max=40;
    EEPROM_READ(TRUST_ADDR,&trusted,sizeof(trusted));
    if(trusted.magic!=TRUST_MAGIC || trusted.checksum!=checksum(&trusted)) memset(&trusted,0,sizeof(trusted));
    GPIOB_ModeCfg(GPIO_Pin_22,GPIO_ModeIN_PU);
    task_id=TMOS_ProcessEventRegister(events);
    memcpy(adv+5,service_uuid,16);
    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED,1,&enable);
    GAPRole_SetParameter(GAPROLE_ADVERT_DATA,sizeof(adv),adv);
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA,sizeof(scan),scan);
    GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL,2,&min);
    GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL,2,&max);
    GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN,interval);GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX,interval);
    GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE,1,&mode);
    GAPBondMgr_SetParameter(GAPBOND_PERI_BONDING_ENABLED,1,&yes);
    GAPBondMgr_SetParameter(GAPBOND_PERI_MITM_PROTECTION,1,&no);
    GAPBondMgr_SetParameter(GAPBOND_PERI_IO_CAPABILITIES,1,&cap);
    GAPBondMgr_SetParameter(GAPBOND_ERASE_AUTO,1,&no);
    GGS_AddService(GATT_ALL_SERVICES);GATTServApp_AddService(GATT_ALL_SERVICES);
    GGS_SetParameter(GGS_DEVICE_NAME_ATT,10,"CH582M-HID");
    GATTServApp_InitCharCfg(INVALID_CONNHANDLE,subscriptions);
    if(GATTServApp_RegisterService(attrs,sizeof(attrs)/sizeof(attrs[0]),GATT_MAX_ENCRYPT_KEY_SIZE,&service_cbs)!=SUCCESS) while(1);
    tmos_set_event(task_id,1);
}
void ble_transport_poll(uint32_t now_ms) {
    unsigned i;
    clock_ms=now_ms;
    if(!GPIOB_ReadPortPin(GPIO_Pin_22)) {
        if(!button_down) {button_down=1;button_since=clock_ms;}
        if(!button_consumed && clock_ms-button_since>=2000) {
            uint8_t mode=GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
            pairing_open=1;pair_deadline=clock_ms+60000;button_consumed=1;
            GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE,1,&mode);
        }
    } else {button_down=button_consumed=0;}
    if(pairing_open && (int32_t)(clock_ms-pair_deadline)>=0) {
        uint8_t mode=GAPBOND_PAIRING_MODE_NO_PAIRING;pairing_open=0;
        GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE,1,&mode);
    }
    for(i=0;i<HD_CONNECTIONS;i++) if(handles[i]!=HD_INVALID) {
        int secure=bond_key(handles[i]) && (pairing_open || known_key());
        hd_mtu(handles[i],ATT_GetMTU(handles[i]));
        hd_security(handles[i],secure);
    }
}
