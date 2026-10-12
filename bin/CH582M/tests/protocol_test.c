#include "hd.h"
#include <string.h>
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
void *memcpy(void *d,const void *s,size_t n) {uint8_t *a=d;const uint8_t *b=s;while(n--) *a++=*b++;return d;}
void *memset(void *d,int v,size_t n) {uint8_t *a=d;while(n--) *a++=(uint8_t)v;return d;}
int memcmp(const void *a,const void *b,size_t n) {const uint8_t *x=a,*y=b;while(n--) {if(*x!=*y)return *x-*y;++x;++y;}return 0;}
void *memmove(void *d,const void *s,size_t n) {uint8_t *a=d;const uint8_t *b=s;if(a>b) {while(n) {--n;a[n]=b[n];}}else memcpy(d,s,n);return d;}
static struct {uint16_t h,n;uint8_t p[244];} output[200];
static unsigned out_count,hid_count,releases;
static int blocked,ready=1,allow=1;
static uint8_t last_cmd;
static int notify(uint16_t h,const uint8_t *p,uint16_t n) {if(blocked)return 0;if(out_count>=200)return 0;output[out_count].h=h;output[out_count].n=n;memcpy(output[out_count++].p,p,n);return 1;}
static int auth(uint16_t h,uint8_t r) {(void)h;(void)r;return allow;}
static uint8_t start(uint8_t c,const uint8_t *p) {(void)p;++hid_count;last_cmd=c;return 0;}
static void release(void) {++releases;}
static int hid_ready(void) {return ready;}
static const hd_ops ops={notify,auth,start,release,hid_ready};
static uint32_t clock;
static void flush(void) {unsigned i;for(i=0;i<20;i++)hd_poll(clock);}
static void reset(void) {hd_init(&ops);out_count=hid_count=releases=0;clock=0;blocked=0;ready=allow=1;}
static int command(uint16_t h,uint16_t seq,const uint8_t *payload,uint16_t n) {uint8_t p[244]={1,0,0,1,0,0};hd_put16(p+1,seq);hd_put16(p+4,n);memcpy(p+6,payload,n);return hd_receive(h,p,n+6);}
static void connect(uint16_t h,uint8_t role) {uint8_t p[]={0x10,role};hd_connect(h);hd_security(h,1);hd_subscribe(h,1);hd_mtu(h,247);command(h,100,p,2);flush();}
static int capture_response(uint16_t seq,uint16_t frame) {uint8_t p[]={1,0,0,0xff,3,0,0,0,0};hd_put16(p+1,seq);hd_put16(p+7,frame);return hd_receive(20,p,9);}
static uint16_t begin_capture(uint16_t seq) {uint8_t p[]={0x11,1,70};command(10,seq,p,3);flush();return hd_le16(output[out_count-1].p+1);}
static int fragment(uint16_t seq,uint16_t frame,uint32_t offset,uint8_t flags,uint32_t total,uint8_t value,uint8_t bytes) {
    uint8_t p[244]={1,0,0,2,0,0};unsigned n=(flags&1)?18:13;
    hd_put16(p+1,seq);hd_put16(p+4,n-6+bytes);hd_put16(p+6,frame);hd_put32(p+8,offset);p[12]=flags;
    if(flags&1) {hd_put32(p+13,total);p[17]=1;}
    memset(p+n,value,bytes);return hd_receive(20,p,n+bytes);
}
static int ack(uint16_t seq,uint16_t frame,uint32_t offset,uint8_t result) {uint8_t p[]={0x13,0,0,0,0,0,0,0};hd_put16(p+1,frame);hd_put32(p+3,offset);p[7]=result;return command(10,seq,p,8);}
int test_main(void) {
    uint8_t click[]={2},move[]={1,100,0,0xce,0xff},ping[]={10},status[]={9},cancel[]={0x12,7,0};
    uint8_t raw[]={1,1,0,1,1,0,2},role_pc[]={0x10,1};
    uint16_t mapped;unsigned count,r;
    reset();CHECK(hd_connect(10));CHECK(hd_connect(20));CHECK(!hd_connect(30));
    CHECK(!hd_receive(10,raw,sizeof(raw)));CHECK(hid_count==0);
    hd_security(10,1);CHECK(!hd_receive(10,raw,sizeof(raw)));hd_subscribe(10,1);
    CHECK(hd_receive(10,raw,sizeof(raw)));flush();CHECK(output[out_count-1].p[6]==HD_DENIED);
    allow=0;command(10,2,role_pc,2);flush();CHECK(output[out_count-1].p[6]==HD_DENIED);
    reset();connect(10,1);connect(20,2);CHECK(out_count==2);
    CHECK(command(10,1,move,5));CHECK(hid_count==1 && last_cmd==1);
    CHECK(command(10,1,move,5));CHECK(hid_count==1);hd_hid_done(0);flush();
    CHECK(hd_le16(output[out_count-1].p+1)==1 && output[out_count-1].p[6]==0);
    CHECK(command(10,1,move,5));flush();CHECK(hid_count==1);
    CHECK(command(10,1,click,1));flush();CHECK(output[out_count-1].p[6]==HD_PARAM);
    CHECK(command(20,2,click,1));flush();CHECK(output[out_count-1].p[6]==HD_DENIED);
    CHECK(command(10,3,click,1));CHECK(hid_count==2);hd_hid_done(0);flush();
    CHECK(command(10,3,click,1));flush();CHECK(hid_count==2);
    CHECK(command(10,4,status,1));flush();CHECK(output[out_count-1].n==8 && output[out_count-1].p[7]==0);
    ready=0;command(10,5,click,1);flush();CHECK(output[out_count-1].p[6]==HD_NO_HID);ready=1;
    /* Screenshot request mapping and repeat-after-lost-response must not re-capture. */
    mapped=begin_capture(6);CHECK(output[out_count-1].h==20);CHECK(mapped!=6);
    CHECK(capture_response(mapped,7));flush();CHECK(output[out_count-1].h==10 && hd_le16(output[out_count-1].p+1)==6);
    count=out_count;begin_capture(6);CHECK(out_count==count+1 && output[out_count-1].h==10);
    /* Stop-and-wait, identical retries, failed ACK then success, last ACK loss. */
    CHECK(fragment(50,7,0,1,4,0xaa,2));flush();CHECK(output[out_count-1].h==10);
    CHECK(!fragment(51,7,2,2,0,0xbb,2));
    CHECK(fragment(50,7,0,1,4,0xaa,2));flush();
    CHECK(ack(7,7,0,1));flush();CHECK(output[out_count-1].h==20);
    CHECK(fragment(50,7,0,1,4,0xaa,2));flush();
    CHECK(ack(8,7,0,0));flush();CHECK(fragment(51,7,2,2,0,0xbb,2));flush();
    CHECK(ack(9,7,2,0));flush();CHECK(fragment(51,7,2,2,0,0xbb,2));flush();
    CHECK(ack(10,7,2,0));flush();CHECK(output[out_count-1].h==20);
    mapped=begin_capture(11);CHECK(output[out_count-1].h==20);CHECK(capture_response(mapped,8));flush();
    CHECK(fragment(52,8,0,3,2,0xaa,2));flush();
    CHECK(!fragment(52,8,0,3,2,0xbb,2));flush();CHECK(output[out_count-1].h==20); /* cancel */
    /* Each destination MTU enforced without re-fragmenting. */
    hd_mtu(10,23);mapped=begin_capture(12);CHECK(capture_response(mapped,7));flush();
    CHECK(!fragment(53,7,0,3,3,0xaa,3));CHECK(fragment(53,7,0,3,2,0xaa,2));flush();
    CHECK(command(10,13,cancel,3));flush();mapped=hd_le16(output[out_count-1].p+1);
    {uint8_t p[]={1,0,0,0xff,1,0,0};hd_put16(p+1,mapped);CHECK(hd_receive(20,p,7));}flush();
    /* Disconnect pending capture returns an error; handle reuse starts clean. */
    begin_capture(14);hd_disconnect(20);flush();CHECK(output[out_count-1].h==10 && output[out_count-1].p[6]==HD_NO_TARGET);
    connect(20,2);CHECK(command(20,15,ping,1));flush();
    /* Main-loop heartbeat and subscription/security loss release keys. */
    CHECK(command(10,16,click,1));count=releases;clock=5000;hd_poll(clock);CHECK(releases==count+1);hd_hid_done(0);
    command(10,17,click,1);flush();CHECK(output[out_count-1].p[6]==HD_DENIED);
    command(10,18,role_pc,2);flush();count=releases;hd_subscribe(10,0);CHECK(releases==count+1);
    reset();connect(10,1);count=releases;hd_security(10,0);CHECK(releases==count+1);CHECK(!command(10,20,click,1));
    /* Backpressure must prevent accepting new side effects when ACK queue is full. */
    reset();connect(10,1);blocked=1;
    for(r=0;r<20;r++) command(10,(uint16_t)(r+1),ping,1);
    count=hid_count;CHECK(!command(10,30,click,1));CHECK(hid_count==count);blocked=0;flush();
    CHECK(command(10,30,click,1));hd_hid_done(0);flush();
    /* Missing phone response times out, is cached, and late replies cannot revive it. */
    reset();connect(10,1);connect(20,2);mapped=begin_capture(40);
    clock=2000;hd_poll(clock);flush();CHECK(output[out_count-1].p[6]==HD_TIMEOUT);
    CHECK(!capture_response(mapped,99));count=out_count;begin_capture(40);
    CHECK(out_count==count+1 && output[out_count-1].h==10 && output[out_count-1].p[6]==HD_TIMEOUT);
    /* Role uniqueness even with both connections authorized. */
    command(20,41,role_pc,2);flush();CHECK(output[out_count-1].p[6]==HD_BUSY);
    /* uint16 Seq wrapping. */
    reset();connect(10,1);command(10,0xffff,click,1);hd_hid_done(0);flush();command(10,0,click,1);hd_hid_done(0);flush();CHECK(hid_count==2);
    /* Fuzz malformed headers/lengths. No reads outside buffers, no unauthorized HID. */
    reset();hd_connect(10);hd_security(10,1);hd_subscribe(10,1);hd_mtu(10,247);
    {uint32_t seed=1;unsigned i,j;uint8_t p[244];for(i=0;i<10000;i++) {for(j=0;j<244;j++){seed=seed*1664525u+1013904223u;p[j]=(uint8_t)(seed>>24);}hd_receive(10,p,(uint16_t)(i%245));hd_poll(0);}CHECK(hid_count==0);}
    return 0;
}
