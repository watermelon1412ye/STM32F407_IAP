# IAP/OTA 远程升级项目进度

最后更新：2026-04-28

## 项目目标

基于 STM32F407 构建 IAP/OTA 远程升级系统：

- Bootloader + APP 分离设计。
- 五个 Flash 分区：boot / 参数区 / 运行区 / 槽位A / 槽位B。
- A/B 双冗余固件存储与启动拷贝流程。
- 串口、以太网 TCP、WiFi 模块(SPI) 三种升级通路。
- APP 基于 FreeRTOS 运行，有可见的任务心跳输出可验证。

## 工程结构

- `IAP_boot/`：Bootloader 工程。
- `IAP_APP_V3/`：FreeRTOS APP 工程，含 lwIP 以太网 OTA。
- `APP/`：Qt 上位机工具，当前聚焦串口升级。

## Flash 分区布局

统一定义在：

- `IAP_boot/User/iap/iap_layout.h`
- `IAP_APP_V3/USER/App/iap_layout.h`

当前分区：

- 启动区：`0x08000000`，大小 `0x0000C000`（48 KB）
- 参数/标志区：`0x0800C000`，大小 `0x00004000`（16 KB）
- 运行区：`0x08010000`，大小 `0x00030000`（192 KB）
- 槽位 A：`0x08040000`，大小 `0x00040000`（256 KB）
- 槽位 B：`0x08080000`，大小 `0x00040000`（256 KB）

Keil 散列输出文件：

- Boot 目标应使用 `IAP_boot/Output/IAP_Bootloadert.sct`。
- APP 目标应使用 `IAP_APP_V3/Output/IAP_APP_V3.sct`。

不要使用旧的测试输出（将全部链接到 `0x08000000`），否则分区设计会被破坏。

## 已完成

### GitHub 仓库

- 仓库地址：https://github.com/watermelon1412ye/STM32F407_IAP
- 已完成初始代码上传，共 539 个文件。
- Git 用户配置：`future1412`
- 已配置本地仓库代理 `http://127.0.0.1:7890`，推送拉取自动走代理。

### Bootloader

实现文件：

- `IAP_boot/User/main.c`
- `IAP_boot/User/iap/iap_uart.c`
- `IAP_boot/User/iap/iap_boot_partition.c`
- `IAP_boot/User/iap/iap_layout.h`

当前行为：

- 复位后等待约 3 秒倒计时。
- 倒计时期间上位机可发送 `ENTER_IAP` 命令 `0x0002`。
- 收到 `ENTER_IAP` 后进入永久串口 IAP 模式。
- 串口 IAP 将固件写入非活跃的 A/B 槽位。
- 接收完毕后写入标志区并复位。
- 正常超时后，读取标志区，将槽位 A 或 B 拷贝到运行区，然后跳转到 `0x08010000`。
- 跳转前校验向量表。

串口帧格式：

- 帧头：`0x5A 0xA5`
- 命令：2 字节
- 负载长度 N：2 字节，大端
- 负载：N 字节
- 保留：4 字节，必须为零
- CRC16-Modbus：2 字节，低字节在前

当前串口命令：

- `0x0001`：固件数据帧
- `0x0002`：进入 IAP 模式

### APP

实现文件：

- `IAP_APP_V3/USER/main.c`
- `IAP_APP_V3/USER/App/ota_update.c`
- `IAP_APP_V3/USER/App/netconf.c`

当前行为：

- APP 运行在 FreeRTOS 之上。
- 创建以太网轮询任务。
- 创建两个打印任务：`task1 alive`、`task2 alive`。
- 初始化 LAN8742A 以太网和 lwIP。
- 启动 TCP OTA 服务器。

TCP OTA 当前细节：

- TCP 服务端口：`6000`
- 使用相同的基本 IAP 帧协议格式。
- 将接收到的固件写入非活跃的 A/B 槽位。
- 空闲超时后写入启动标志。
- 调用 `NVIC_SystemReset()` 复位，交由 Bootloader 拷贝槽位固件到运行区。

### Qt 上位机工具

实现文件：

- `APP/dialog.cpp`
- `APP/dialog.h`
- `APP/APP.pro`

当前行为：

- 使用 Qt SerialPort 串口通信。
- 可打开串口。
- 可选择 `.bin` 固件文件。
- 可发送 `ENTER_IAP` 命令。
- 可将固件拆分为 256 字节负载帧。
- 可构建带 CRC16-Modbus 的帧并自动发送。
- 有串口升级进度显示。

## 尚未完成

### WiFi 模块 SPI OTA

这是最大的待完成需求。目前仅有 STM32 SPI 库基础和预留的 SPI 相关文件，没有完整的 WiFi 模块驱动、SPI 传输协议、OTA 接收任务或固件写入通路。

需要添加：

- WiFi 模块选型与接口定义。
- 模块的 SPI 驱动封装。
- WiFi OTA 协议处理。
- 固件下载写入非活跃槽位。
- 标志更新与复位流程。

### PC 工具以太网 TCP 升级

MCU 端 TCP OTA 已实现，但 Qt 工具目前聚焦串口升级。

如需满足演示要求：

- TCP 连接界面：目标 IP 和端口。
- 复用 IAP 帧格式的 TCP 帧发送。
- 复用串口通路的固件分片与进度逻辑。

### 整包完整性校验

当前实现有逐帧 CRC16 和向量表校验。为更强地满足考核要求，需添加整包校验：

- 在升级开始/结束命令中包含总固件大小和全包 CRC。
- Boot/APP 整包 CRC 通过后才写标志。
- 将全包 CRC 存储在参数区。
- Boot 可选地在拷贝到运行区前校验源槽位。

### 文档与测试凭证

需收集最终演示凭证：

- 串口升级日志。
- 以太网 TCP 升级日志。
- WiFi OTA 升级日志。
- Boot 倒计时日志。
- A/B 槽位切换日志。
- APP 成功跳转后的任务打印日志。
- 如需要，Ping/TCP 连接截图。

## 建议后续工作顺序

1. 确认 Boot 和 APP 能以正确的 Keil 目标编译通过。
2. 烧录 Bootloader 到 `0x08000000`。
3. 生成链接在 `0x08010000` 的 APP bin 文件。
4. 用 Qt 工具测试串口升级。
5. 测试 Boot 拷贝 A/B 到运行区及 APP 任务打印。
6. 测试以太网 TCP OTA（端口 `6000`）。
7. 如需，添加 Qt TCP 发送功能。
8. 添加 WiFi SPI OTA 通路。
9. 添加全包 CRC 并更新标志结构。
10. 完善 README 和演示流程文档。

## 当前风险备注

- 手动烧录 APP 时不要覆盖 Boot 区域。
- 进行 IAP 测试时不要使用链接在 `0x08000000` 的 APP bin。
- 当前标志结构简单：magic/version/boot_from/image_size，尚未存储全包 CRC。
- TCP OTA 在空闲超时后提交，可满足测试但需要明确记录此行为。
- WiFi/SPI OTA 尚未实现，在宣称三种通信方式全部完成前必须处理。
