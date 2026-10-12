# HandDouble

基于 WCH CH582M 的蓝牙与 USB HID 硬件项目，支持键盘、鼠标控制和截图数据中继。

## 目录

- [`hardware/`](hardware/)：硬件设计工程 `HandDouble.epro2`。
- [`bin/CH582M/`](bin/CH582M/)：CH582M 固件、构建脚本和测试。

## 构建

可使用 MounRiver 打开固件工程，或在 PowerShell 中运行：

```powershell
cd bin/CH582M
./build.ps1
```

编译需要 RISC-V 工具链。构建输出位于固件目录的 `build/` 下。

详细的编译、烧录、蓝牙协议和联调说明见 [固件 README](bin/CH582M/README.md)。目前尚未完成实机验收。
