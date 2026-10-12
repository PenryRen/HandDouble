#include "CONFIG.h"
#include "HAL.h"
#include "hd.h"
#include "usb_hid.h"
#include "ble_transport.h"

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE/4];
int main(void) {
    static const hd_ops ops={ble_notify,ble_authorize_role,usb_hid_start,usb_hid_release,usb_hid_ready};
    uint32_t previous,ms=0,remainder=0;
    SetSysClock(CLK_SOURCE_PLL_60MHz);
    PWR_DCDCCfg(ENABLE);
    hd_init(&ops);
    CH58X_BLEInit();HAL_Init();GAPRole_PeripheralInit();
    usb_hid_init();ble_transport_init();
    previous=TMOS_GetSystemClock();
    WWDG_SetCounter(0);WWDG_ResetCfg(ENABLE);
    while(1) {
        uint32_t current,delta;
        TMOS_SystemProcess();
        current=TMOS_GetSystemClock();delta=current-previous;previous=current;
        /* TMOS ticks are 625 us; preserve fractional ms and counter wrap. */
        remainder+=delta*5;ms+=remainder/8;remainder%=8;
        ble_transport_poll(ms);usb_hid_poll(ms);hd_poll(ms);
        WWDG_SetCounter(0); /* ~559 ms at 60 MHz; main-loop liveness only */
    }
}
