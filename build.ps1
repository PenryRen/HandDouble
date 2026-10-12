param([string]$Toolchain = '', [switch]$Tests, [string]$Python = '')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (-not $Toolchain) {
    $Toolchain = 'C:/MounRiver/MounRiver_Studio2/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC/bin'
}
$compiler = Join-Path $Toolchain 'riscv-none-embed-gcc.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw "Compiler not found: $compiler. Pass -Toolchain <bin directory>." }
New-Item -ItemType Directory -Force build | Out-Null
$flags = @('-march=rv32imac','-mabi=ilp32','-msmall-data-limit=0','-Os','-g','-std=gnu99',
    '-ffunction-sections','-fdata-sections','-fno-common','-Wall','-Wextra',
    '-Isrc','-IStdPeriphDriver/inc','-IRVMSIS','-Isdk/BLE/LIB','-Isdk/BLE/HAL/include')
$files = @('src/Main.c','src/hd.c','src/usb_hid.c','src/ble_transport.c',
    'sdk/BLE/HAL/MCU.c','sdk/BLE/HAL/RTC.c','RVMSIS/core_riscv.c','Startup/startup_CH583.S',
    'StdPeriphDriver/CH58x_sys.c','StdPeriphDriver/CH58x_clk.c','StdPeriphDriver/CH58x_pwr.c',
    'StdPeriphDriver/CH58x_gpio.c','StdPeriphDriver/CH58x_uart1.c','StdPeriphDriver/CH58x_adc.c',
    'StdPeriphDriver/CH58x_flash.c')
$objects = @()
foreach ($file in $files) {
    $object = 'build/' + ($file -replace '[/\\.]','_') + '.o'
    & $compiler @flags -c $file -o $object
    if ($LASTEXITCODE) { throw "Compile failed: $file" }
    $objects += $object
}
$linkFlags = @('-nostartfiles','--specs=nano.specs','--specs=nosys.specs',
    '-TLd/Link.ld','-Wl,--gc-sections','-Wl,--print-memory-usage','-Wl,-Map,build/HandDouble.map',
    '-Lsdk/BLE/LIB','-LStdPeriphDriver','-lCH58xBLE','-lISP583','-o','build/HandDouble.elf')
& $compiler @flags @objects @linkFlags
if ($LASTEXITCODE) { throw 'Link failed' }
& (Join-Path $Toolchain 'riscv-none-embed-objcopy.exe') -O ihex build/HandDouble.elf build/HandDouble.hex
if ($LASTEXITCODE) { throw 'HEX generation failed' }
& (Join-Path $Toolchain 'riscv-none-embed-objcopy.exe') -O binary build/HandDouble.elf build/HandDouble.bin
if ($LASTEXITCODE) { throw 'BIN generation failed' }
& (Join-Path $Toolchain 'riscv-none-embed-size.exe') build/HandDouble.elf
if ($Tests) {
    if (-not $Python) { $Python = 'C:/Users/penry/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' }
    & $Python tests/make_mock.py
    if ($LASTEXITCODE) { throw 'USB mock generation failed' }
    $testFlags = @('-march=rv32im','-mabi=ilp32','-msmall-data-limit=0','-Os','-g','-std=c99',
        '-fno-builtin','-nostdlib','-Isrc','-Ttests/test.ld')
    & $compiler @testFlags tests/start.S tests/protocol_test.c src/hd.c -o build/protocol_test.elf
    if ($LASTEXITCODE) { throw 'Protocol test compilation failed' }
    & $compiler @testFlags '-Itests/mock' tests/start.S tests/usb_test.c tests/mock/registers.c src/usb_hid.c -o build/usb_test.elf
    if ($LASTEXITCODE) { throw 'USB test compilation failed' }
    & $Python tests/run.py protocol_test
    if ($LASTEXITCODE) { throw 'Protocol test failed' }
    & $Python tests/run.py usb_test
    if ($LASTEXITCODE) { throw 'USB test failed' }
}
