# HandDouble / CH582M 固件

按《硬件蓝牙传输层协议 V1.1》《硬件蓝牙应用层协议 V1.1》实现。
线上 Version 为 `0x01`，小端 6 字节包头；RX 每次 Write with Response
接收一个完整包，TX 每次 Notify 发送一个完整包。

## 已实现

- USB 复合 HID：接口 0 为 8 字节 boot 键盘，接口 1 为五键相对鼠标，
  支持垂直滚轮和水平 AC Pan；boot 鼠标模式为 3 字节报告。
- 应用层 `0x01`～`0x0A`、`0x10`～`0x13` 全部指令，严格校验长度与参数，
  16 位有符号位移拆为多次 8 位相对报告。HID 成功 ACK 在最后一个报告完成 IN
  传输后发出，不代表手机 App 已响应操作。最多同时执行一条 HID 指令；忙时返回 `0x04`。
- 两个 Peripheral 连接槽，电脑/手机角色唯一，各自维护连接句柄、CCCD、MTU。
- 150 ms 可连接广播；名称 `CH582M-HID`，广播包含协议规定的 128-bit Service UUID。
  UUID 不变：Service `9a3ba77f-acfd-4e84-8594-6d9fffbf63d9`，
  RX `9a3ba780-acfd-4e84-8594-6d9fffbf63d9`，TX `9a3ba781-acfd-4e84-8594-6d9fffbf63d9`。
- 配对、绑定、加密和物理授权；角色持久关联到 bond LTK，避免使用会轮换的 BLE MAC
  作为认证依据。每个身份只能拥有一个角色，重连需要重新订阅和注册。
- 请求结果缓存：每连接最近 8 个请求，保留 10 秒；相同 Seq/内容返回原结果，
  相同 Seq/不同内容拒绝。未完成 HID 和截图请求也会去重。busy 可使用原 Seq 重试。
- 单截图任务、请求 Seq 映射、停止等待中继、字节级分片重传一致性检查，
  不缓存整个 JPEG；只保留一个最大 244 字节分片和 8 个发送队列槽。
  末片完成后短暂保留重复分片状态，允许丢失末片 ACK 的恢复。
- 电脑断线、关闭 Notify、失去授权或心跳超时均释放键盘/鼠标并取消任务。
  手机断线向电脑报告 `0x06`，请求超时报告 `0x07`；取消、超时清理中继状态。
- 硬件看门狗只由正常运行的主循环喂狗，60 MHz 下约 559 ms 溢出复位。

## 编译与烧录

工程已同步 `.cproject`，可在 MounRiver 中构建。也可在工程根目录运行：

MounRiver 2 的扫描器遍历整个工程，仅使用 sourceEntries 的 excluding 字段过滤，
不会把独立 sourcePath 当作白名单。根目录明确排除 `vendor`、`tests`、`docs`、
`build`、`obj`、`LIB`；保留 `src` 和所需 `sdk` HAL 源码。
工程自有 `makefile.defs` 会在 IDE 尚未重新扫描 SDK 时补充缺失的 MCU/RTC 对象；
正常扫描后自动跳过补充项。不要删除此文件，也不需要手改 `obj` 下生成的 makefile。

```powershell
./build.ps1
# 其他电脑可指定工具链 bin 目录
./build.ps1 -Toolchain 'C:/path/to/RISC-V Embedded GCC/bin'
```

输出 `build/HandDouble.hex`、`build/HandDouble.bin`、`build/HandDouble.elf` 和 map。
这是使用官方 `LIBCH58xBLE.a` 链接的完整程序，不使用 BLE ROM 分区映像，
从地址 0 烧录。原 UART 示例保存在 `docs/Main.uart-example.c.bak`。
尚未对实体板进行烧写。

## 原理图对应配置

USB 使用芯片 USB D-/D+（PB10/PB11）；外部 32 MHz 晶振和 32.768 kHz 晶振；
启用 DC/DC（原理图 VSW 到 VDCID 的电感）。BOOT 按钮使用 PB22，低电平按下。
没有占用 UART、LED 或额外按键。USB 运行期间禁用低功耗睡眠。

运行中的板子长按 BOOT 2 秒，打开 60 秒首次配对/角色授权窗口。
在窗口内按顺序完成：连接 → 配对绑定并加密 → 发现服务 → 订阅 TX Notify →
REGISTER_ROLE。电脑角色 `1`，手机角色 `2`。
已有授权绑定设备可在窗口关闭后重连。需要更换某角色时，在窗口内注册新身份。
窗口外新设备不能获得 HID 或截图权限。
上电/复位时按住 BOOT 可能进入芯片下载模式，配对操作应在程序运行后进行。

采用 Just Works + 物理窗口；并未宣称具备 MITM 防护。不要在非预期客户端
靠近时打开授权窗口。可信角色记录存于 data-flash `0x7c00..0x7dff`，
BLE SDK 的绑定数据位于 `0x7e00..0x7fff`。程序升级保留 data-flash 可保留身份；
清除绑定时需同时清除两块区域并在客户端删除旧绑定。

USB VID/PID 暂沿用 WCH 示例 `413D:2107`，用于开发验证；量产应换成获授权的标识。

## 客户端联调约定

1. 必须加密、绑定并订阅 Notify，业务前注册角色。禁止 Write without Response、
   Prepare Write / 长写；按一个完整包一次 Write。
2. MTU 由 Central 发起协商，固件支持最多 247，逐连接读取实际 MTU。
   协议现有 `get_status` 只返回 DeviceStatus，没有 MTU 字段，因此未获知两条链路 MTU
   时，手机必须保守使用 MTU 23：首片最多 2 字节 JPEG，普通片最多 7 字节。
   客户端在带外确认两条链路 MTU 后才可增大分片；固件不会擅自给 V1.1 增加字段。
3. `ping` 建议每秒发送，电脑超过 5 秒无有效业务/心跳后松键、撤销当前角色、
   取消截图。连接仍保留，需重新 REGISTER_ROLE 才能执行 HID。这是文档未指定的
   心跳默认值，可修改 `HD_HEARTBEAT_MS`。
4. 截图请求/取消响应等待 2 秒。分片等待窗口 8 秒，覆盖手机初次发送及
   3 次按 2 秒间隔的重试。手机必须等待电脑 transfer_ack，再发下一片。
   电脑丢失 ACK 后可再次确认同一分片，末片也支持这一恢复过程。
5. 缓冲耗尽、乱序或超 MTU 的包通过 ATT 错误拒绝，客户端应保持包内容和 Seq 重试，
   不能把 GATT 成功当作应用层完成。`transfer_ack` 不生成应用层响应。
6. 手机分片的 FrameID/Offset/Flags/内容保持原样转发，电脑请求/响应按映射恢复 Seq。
   JPEG 重组和完整格式检查在电脑端完成，固件校验元数据、最大 1 MiB、
   连续偏移、末片边界及保留分片的重传内容。
7. V1.1 没有独立的“截图中断事件”类型。接受任务后的异常用原 capture 请求 Seq
   发送错误 Response，客户端需在收到成功响应后仍允许此 Seq 的终止错误。
8. 手机 USB 必须提供 host/OTG 能力；FPC、Type-C CC、充电同时传数据的硬件行为
   无法由固件补足，需要按实物另行确认。

## 验证与实际限制

```powershell
# Python 需安装 unicorn==2.1.4（可放入 tests/python-deps）
python -m pip install --target tests/python-deps unicorn==2.1.4
./build.ps1 -Tests -Python 'C:/path/to/python.exe'
```

测试在 Unicorn RISC-V 中执行实际交叉编译的 C 代码，而非另一份协议实现。
协议测试包括角色/权限、请求去重、截图 Seq 映射、丢失/失败 ACK、末片重传、
不同 MTU、取消、断线、心跳、缓冲背压、序号循环和 10,000 个畸形包。
USB 测试使用模拟寄存器，覆盖描述符响应、枚举状态、按键映射、
最大正负位移、点击、六键和 modifier、双滚轮、boot 模式和超时/复位松键。

交叉编译和仿真通过不等于实机验收：仍需检查 Android/iOS 枚举兼容性、
两个 Central 同时连接、加密重连时 SDK linkDB 标志/LTK 的恢复、绑定 data-flash
持久性、247 MTU 协商、Notify 背压、射频吞吐和物理按键。
官方 SDK 的多连接示例及 ConnectNumber 配置支持实现该设计，未声称已测得双链路性能。
尚未烧写用户设备。
