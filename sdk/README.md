# WCH SDK dependencies

Upstream: https://github.com/openwch/ch583

Downloaded on 2026-10-13 from
`https://codeload.github.com/openwch/ch583/zip/refs/heads/main`.
Archive SHA-256:
`B029C4965C940652C0F34F8A92970262E7D45789964673317961BA9629BE9866`.
The original downloaded archive and source are in the excluded `vendor/` directory.

Only the BLE V2.10 header/static library and HAL MCU/RTC sources/headers are
included in the firmware build. All original WCH copyright and chip-use notices
are retained. `BLE/HAL/include/config.h` is replaced with a one-line adapter to
the project's `src/CONFIG.h`, so every translation unit uses the same heap,
two Peripheral connections, MTU buffers, external oscillator and SNV settings.
The original project peripheral drivers and ISP static library are retained.

The vendor source's MultiCentPeri example configures multiple Peripheral links;
`bleConfig_t.ConnectNumber` packs the Peripheral count into bits 0..1.
This firmware uses 2 Peripheral and 0 Central connections. Hardware validation
is still required; this source example is not proof of tested throughput.
