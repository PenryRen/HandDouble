#include "hd.h"
#include <string.h>

#define TX_SLOTS 8
#define CACHE_SLOTS 8
#define CACHE_MS 10000u
typedef struct {
    uint16_t seq;
    uint32_t time;
    uint8_t valid, request_len, request[8], response_len, response[3];
} result_cache;
typedef struct {
    uint16_t handle, mtu;
    uint8_t role, secure, subscribed, next_cache;
    uint32_t last_rx;
    result_cache cache[CACHE_SLOTS];
} connection;
typedef struct { uint16_t handle, length; uint8_t bytes[HD_MAX_PACKET]; } tx_item;
typedef struct {
    uint8_t active, pending, pending_cmd, frame_valid, waiting_ack, last_seen;
    uint16_t source_seq, original_seq, forwarded_seq, frame, fragment_seq;
    uint8_t pending_request[3], original_request[3];
    uint32_t deadline, total, next_offset, fragment_offset, fragment_size;
    uint16_t fragment_length;
    uint8_t fragment[HD_MAX_PACKET]; /* one retained fragment, never a full JPEG */
} capture_state;
static hd_ops io;
static connection links[HD_CONNECTIONS];
static tx_item tx[TX_SLOTS];
static uint8_t tx_count;
static uint16_t relay_seq;
static uint32_t now;
static capture_state capture;
static struct {
    uint8_t active, request_len, request[8];
    uint16_t handle, seq;
} hid;

uint16_t hd_le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1]<<8); }
uint32_t hd_le32(const uint8_t *p) { return hd_le16(p) | ((uint32_t)hd_le16(p+2)<<16); }
void hd_put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
void hd_put32(uint8_t *p, uint32_t v) { hd_put16(p,(uint16_t)v); hd_put16(p+2,(uint16_t)(v>>16)); }
static connection *find(uint16_t h) {
    unsigned i;
    for(i=0;i<HD_CONNECTIONS;i++) if(links[i].handle==h && h!=HD_INVALID) return &links[i];
    return NULL;
}
static connection *role(uint8_t r) {
    unsigned i;
    for(i=0;i<HD_CONNECTIONS;i++)
        if(links[i].handle!=HD_INVALID && links[i].role==r && links[i].secure && links[i].subscribed) return &links[i];
    return NULL;
}
static void drop_tx(uint16_t handle) {
    unsigned i=0;
    while(i<tx_count) {
        if(tx[i].handle==handle) { memmove(tx+i,tx+i+1,(tx_count-i-1)*sizeof(tx[0])); --tx_count; }
        else ++i;
    }
}
static void drop_capture_tx(void) {
    unsigned i=0;
    connection *pc=role(HD_PC), *phone=role(HD_PHONE);
    while(i<tx_count) {
        uint8_t *p=tx[i].bytes;
        uint16_t seq=hd_le16(p+1);
        int remove=(pc && tx[i].handle==pc->handle &&
                    (p[3]==2 || (p[3]==0xff && (seq==capture.original_seq || seq==capture.source_seq)))) ||
                   (phone && tx[i].handle==phone->handle && p[3]==1 && p[6]>=0x11 && p[6]<=0x13);
        if(remove) {memmove(tx+i,tx+i+1,(tx_count-i-1)*sizeof(tx[0]));--tx_count;}
        else ++i;
    }
}
static int enqueue(uint16_t h, const uint8_t *p, uint16_t n) {
    connection *c=find(h);
    if(!c || !c->secure || !c->subscribed || n>c->mtu-3 || n>HD_MAX_PACKET || tx_count==TX_SLOTS) return 0;
    tx[tx_count].handle=h; tx[tx_count].length=n;
    memcpy(tx[tx_count++].bytes,p,n);
    return 1;
}
static int response(connection *c, uint16_t seq, const uint8_t *payload, uint8_t n) {
    uint8_t p[9]={1,0,0,0xff,0,0};
    hd_put16(p+1,seq); hd_put16(p+4,n); memcpy(p+6,payload,n);
    return enqueue(c->handle,p,(uint16_t)(n+6));
}
static void finish(connection *c, uint16_t seq, const uint8_t *req, uint8_t req_len,
                   const uint8_t *res, uint8_t res_len) {
    result_cache *r;
    /* Transient backpressure must be retryable with the same Seq. */
    if(req_len && req_len<=8 && res[0]!=HD_BUSY) {
        r=&c->cache[c->next_cache++ % CACHE_SLOTS];
        r->valid=1; r->seq=seq; r->time=now; r->request_len=req_len;
        memcpy(r->request,req,req_len); r->response_len=res_len; memcpy(r->response,res,res_len);
    }
    response(c,seq,res,res_len);
}
static void abort_capture(uint8_t status) {
    connection *pc=role(HD_PC), *phone=role(HD_PHONE);
    uint8_t p[9]={1,0,0,1,3,0,0x12,0,0};
    if(!capture.active) return;
    /* Discard queued frame data and stale capture ACKs before reporting failure. */
    drop_capture_tx();
    if(pc) {
        if(capture.pending) finish(pc,capture.source_seq,capture.pending_request,3,&status,1);
        if(!capture.pending || capture.source_seq!=capture.original_seq)
            finish(pc,capture.original_seq,capture.original_request,3,&status,1);
    }
    if(phone && capture.frame_valid) {
        hd_put16(p+1,relay_seq++); hd_put16(p+7,capture.frame);
        enqueue(phone->handle,p,sizeof(p));
    }
    memset(&capture,0,sizeof(capture));
}
void hd_init(const hd_ops *ops) {
    unsigned i;
    io=*ops; memset(links,0,sizeof(links));
    for(i=0;i<HD_CONNECTIONS;i++) links[i].handle=HD_INVALID;
    tx_count=0; relay_seq=0; now=0;
    memset(&capture,0,sizeof(capture)); memset(&hid,0,sizeof(hid));
}
int hd_connect(uint16_t handle) {
    unsigned i;
    if(handle==HD_INVALID || find(handle)) return 0;
    for(i=0;i<HD_CONNECTIONS;i++) if(links[i].handle==HD_INVALID) {
        memset(&links[i],0,sizeof(links[i])); links[i].handle=handle;
        links[i].mtu=23; links[i].last_rx=now; return 1;
    }
    return 0;
}
void hd_disconnect(uint16_t handle) {
    connection *c=find(handle);
    if(!c) return;
    if(c->role==HD_PC) { io.hid_release(); hid.active=0; }
    c->secure=0; drop_tx(handle);
    if(c->role) { abort_capture(HD_NO_TARGET); memset(&capture,0,sizeof(capture)); }
    memset(c,0,sizeof(*c)); c->handle=HD_INVALID;
}
void hd_security(uint16_t handle, int secure) {
    connection *c=find(handle);
    if(!c) return;
    if(c->secure && !secure) {
        if(c->role==HD_PC) { io.hid_release(); hid.active=0; }
        c->secure=0;
        drop_tx(handle); abort_capture(HD_DENIED); c->role=0; memset(c->cache,0,sizeof(c->cache));
    }
    c->secure=(uint8_t)!!secure;
}
void hd_subscribe(uint16_t handle, int enabled) {
    connection *c=find(handle);
    if(!c) return;
    if(c->subscribed && !enabled) {
        if(c->role==HD_PC) { io.hid_release(); hid.active=0; }
        c->subscribed=0; drop_tx(handle); abort_capture(HD_NO_TARGET); c->role=0;
        memset(c->cache,0,sizeof(c->cache));
    }
    c->subscribed=(uint8_t)!!enabled;
}
void hd_mtu(uint16_t h,uint16_t mtu) {
    connection *c=find(h);
    if(c) c->mtu=mtu<23?23:(mtu>247?247:mtu);
}
uint16_t hd_link_mtu(uint16_t h) { connection *c=find(h); return c?c->mtu:23; }
void hd_hid_done(uint8_t status) {
    connection *c;
    if(!hid.active) return;
    c=find(hid.handle);
    if(c && c->secure && c->role==HD_PC) finish(c,hid.seq,hid.request,hid.request_len,&status,1);
    hid.active=0;
}
static int data_packet(connection *c,const uint8_t *p,uint16_t n) {
    connection *pc=role(HD_PC);
    const uint8_t *v=p+6;
    uint16_t seq=hd_le16(p+1), payload=hd_le16(p+4), overhead;
    uint32_t offset,size,total;
    uint8_t flags;
    if(c->role!=HD_PHONE || !pc || !capture.frame_valid ||
       (!capture.active && (!capture.last_seen || (int32_t)(now-capture.deadline)>=0))) return 0;
    if(capture.pending || payload<7 || hd_le16(v)!=capture.frame || n>pc->mtu-3) return 0;
    flags=v[6]; offset=hd_le32(v+2); overhead=(flags&1)?12:7;
    if(flags>3 || payload<overhead) return 0;
    size=payload-overhead;
    if(!size) return 0;
    if(capture.waiting_ack) {
        /* Byte-for-byte identical retry is allowed; changed content aborts. */
        if(seq!=capture.fragment_seq && offset!=capture.fragment_offset) return 0;
        if(seq!=capture.fragment_seq || n!=capture.fragment_length || memcmp(p,capture.fragment,n)) {
            abort_capture(HD_FAILED); return 0;
        }
        return enqueue(pc->handle,p,n);
    }
    /* Lost ACK: resend the already accepted last fragment without advancing state. */
    if(capture.fragment_length && seq==capture.fragment_seq) {
        if(n!=capture.fragment_length || memcmp(p,capture.fragment,n)) { abort_capture(HD_FAILED); return 0; }
        return enqueue(pc->handle,p,n);
    }
    if(offset!=capture.next_offset || capture.last_seen) return 0;
    total=capture.total;
    if(flags&1) {
        if(offset || total || v[11]!=1) return 0;
        total=hd_le32(v+7);
        if(!total || total>1024u*1024u) return 0;
    } else if(!total) return 0;
    if(offset>total || size>total-offset || (!!(flags&2)!=(offset+size==total))) return 0;
    if(!enqueue(pc->handle,p,n)) return 0;
    capture.total=total; capture.fragment_seq=seq; capture.fragment_offset=offset;
    capture.fragment_size=size; capture.fragment_length=n; memcpy(capture.fragment,p,n);
    capture.waiting_ack=1; capture.last_seen=!!(flags&2); capture.deadline=now+HD_FRAGMENT_MS;
    return 1;
}
int hd_receive(uint16_t handle,const uint8_t *p,uint16_t n) {
    connection *c=find(handle), *target;
    uint16_t seq,length;
    uint8_t status=HD_OK, res[3]={0}, res_len=1, cmd;
    unsigned i;
    uint8_t relay[HD_MAX_PACKET];
    if(!c || !c->secure || !c->subscribed || n<6 || n>HD_MAX_PACKET || n>c->mtu-3) return 0;
    seq=hd_le16(p+1); length=hd_le16(p+4);
    if(p[0]!=1 || length!=n-6) { status=HD_PARAM; response(c,seq,&status,1); return 1; }
    if(p[3]==2) {
        int accepted=data_packet(c,p,n);
        if(accepted) c->last_rx=now;
        return accepted;
    }
    if(p[3]==0xff) {
        connection *pc=role(HD_PC);
        if(c->role!=HD_PHONE || !capture.active || !capture.pending || seq!=capture.forwarded_seq || !pc || !length) return 0;
        if(p[6]>HD_DENIED || (p[6]==HD_OK && length!=(capture.pending_cmd==0x11?3:1)) || (p[6]!=HD_OK && length!=1)) return 0;
        if(tx_count>=TX_SLOTS-1) return 0;
        finish(pc,capture.source_seq,capture.pending_request,3,p+6,(uint8_t)length);
        capture.pending=0; c->last_rx=now;
        if(p[6]!=HD_OK || capture.pending_cmd==0x12) { memset(&capture,0,sizeof(capture)); }
        else { capture.frame=hd_le16(p+7); capture.frame_valid=1; capture.deadline=now+HD_FRAGMENT_MS; }
        return 1;
    }
    if(p[3]!=1 || !length) { status=HD_PARAM; response(c,seq,&status,1); return 1; }
    cmd=p[6];
    if(cmd!=0x10 && !c->role) { status=HD_DENIED; response(c,seq,&status,1); return 1; }
    /* Reserve space before allowing any side effect; ACKs cannot be silently lost. */
    if(tx_count>=TX_SLOTS-2) return 0;
    for(i=0;i<CACHE_SLOTS;i++) {
        result_cache *r=&c->cache[i];
        if(r->valid && r->seq==seq && now-r->time<CACHE_MS) {
            if(length!=r->request_len || memcmp(p+6,r->request,length)) { status=HD_PARAM; response(c,seq,&status,1); }
            else { response(c,seq,r->response,r->response_len); c->last_rx=now; }
            return 1;
        }
    }
    if(hid.active && hid.handle==handle && hid.seq==seq) {
        if(length!=hid.request_len || memcmp(p+6,hid.request,length)) { status=HD_PARAM; response(c,seq,&status,1); }
        return 1;
    }
    if(capture.active && capture.pending && c->role==HD_PC && seq==capture.source_seq &&
       (length!=3 || memcmp(p+6,capture.pending_request,3))) {
        status=HD_PARAM;response(c,seq,&status,1);return 1;
    }
    if(cmd==0x10) {
        if(length!=2 || (p[7]!=HD_PC && p[7]!=HD_PHONE)) status=HD_PARAM;
        else if((c->role && c->role!=p[7]) || ((target=role(p[7])) && target!=c)) status=HD_BUSY;
        else if(!io.authorize_role(handle,p[7])) status=HD_DENIED;
        else { c->role=p[7]; c->last_rx=now; }
    } else if(cmd==9 || cmd==10) {
        if(length!=1) status=HD_PARAM;
        else { c->last_rx=now; if(cmd==9) { res[1]=0; res_len=2; } }
    } else if(cmd>=1 && cmd<=8) {
        static const uint8_t lens[]={0,5,1,2,2,5,2,2,1};
        if(c->role!=HD_PC) status=HD_DENIED;
        else if(length!=lens[cmd]) status=HD_PARAM;
        else if(!io.hid_ready()) status=HD_NO_HID;
        else if(hid.active && cmd!=8) status=HD_BUSY;
        else {
            if(cmd==8 && hid.active) { io.hid_release(); hd_hid_done(HD_FAILED); }
            status=io.hid_start(cmd,p+7);
            if(status==HD_OK) {
                hid.active=1; hid.handle=handle; hid.seq=seq; hid.request_len=(uint8_t)length;
                memcpy(hid.request,p+6,length); c->last_rx=now; return 1;
            }
        }
    } else if(cmd>=0x11 && cmd<=0x13) {
        if(c->role!=HD_PC) status=HD_DENIED;
        else if(length!=(cmd==0x11?3:cmd==0x12?3:8)) status=HD_PARAM;
        else if(!(target=role(HD_PHONE))) status=HD_NO_TARGET;
        else if(cmd==0x11 && (p[7]!=1 || p[8]<1 || p[8]>100)) status=HD_PARAM;
        else if(cmd==0x11 && capture.active) {
            if(capture.pending_cmd==cmd && capture.source_seq==seq) {
                if(memcmp(p+6,capture.pending_request,3)) { status=HD_PARAM; response(c,seq,&status,1); }
                return 1;
            }
            status=HD_BUSY;
        } else if(cmd!=0x11 && ((!capture.active && !(cmd==0x13 && capture.last_seen && (int32_t)(now-capture.deadline)<0)) ||
                  !capture.frame_valid || hd_le16(p+7)!=capture.frame)) status=HD_PARAM;
        else if(cmd==0x13 && (p[13]>1 || hd_le32(p+9)!=capture.fragment_offset || !capture.fragment_length)) status=HD_PARAM;
        else if(cmd==0x12 && capture.pending) {
            if(capture.pending_cmd==cmd && capture.source_seq==seq) return 1;
            status=HD_BUSY;
        } else {
            if(cmd==0x12) drop_capture_tx();
            memcpy(relay,p,n); hd_put16(relay+1,relay_seq++);
            if(!enqueue(target->handle,relay,n)) return 0;
            c->last_rx=now;
            if(cmd==0x13) {
                if(p[13]==0 && capture.waiting_ack) {
                    capture.next_offset=capture.fragment_offset+capture.fragment_size; capture.waiting_ack=0;
                    if(capture.last_seen) { capture.active=0;capture.deadline=now+HD_FRAGMENT_MS;return 1; }
                }
                capture.deadline=now+HD_FRAGMENT_MS; return 1; /* no response */
            }
            if(cmd==0x11) {
                memset(&capture,0,sizeof(capture));capture.original_seq=seq;
                memcpy(capture.original_request,p+6,3);
            }
            capture.active=1; capture.pending=1; capture.pending_cmd=cmd;
            capture.source_seq=seq; capture.forwarded_seq=hd_le16(relay+1);
            memcpy(capture.pending_request,p+6,3);
            capture.deadline=now+HD_CAPTURE_MS; return 1;
        }
        if(cmd==0x13) return status==HD_OK; /* ACK never elicits a response */
    } else status=HD_UNKNOWN;
    res[0]=status;
    if(status) res_len=1;
    finish(c,seq,p+6,(uint8_t)(length<=8?length:0),res,res_len);
    return 1;
}
void hd_poll(uint32_t now_ms) {
    unsigned i;
    now=now_ms;
    for(i=0;i<HD_CONNECTIONS;i++) {
        connection *c=&links[i];
        if(c->handle!=HD_INVALID && c->role==HD_PC && now-c->last_rx>=HD_HEARTBEAT_MS) {
            io.hid_release(); hid.active=0; abort_capture(HD_TIMEOUT);
            c->role=0; memset(c->cache,0,sizeof(c->cache));
        }
    }
    if(capture.active && (int32_t)(now-capture.deadline)>=0) abort_capture(HD_TIMEOUT);
    /* Send at most one packet per link per poll; a congested link cannot starve the other. */
    for(i=0;i<HD_CONNECTIONS;i++) {
        unsigned j;
        for(j=0;j<tx_count;j++) if(tx[j].handle==links[i].handle) {
            connection *c=&links[i];
            if(tx[j].length>c->mtu-3) { abort_capture(HD_PARAM); break; }
            if(c->secure && c->subscribed && io.notify(c->handle,tx[j].bytes,tx[j].length)) {
                memmove(tx+j,tx+j+1,(tx_count-j-1)*sizeof(tx[0])); --tx_count;
            }
            break;
        }
    }
}
