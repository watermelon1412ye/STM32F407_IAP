# IAP_APP — FreeRTOS 应用程序

## 1. 工程概述

`IAP_APP_V3` 是基于 STM32F407ZGT6 + FreeRTOS + lwIP 的 IAP 应用程序工程。系统上电后由 Bootloader 拷贝固件至运行区（`0x08010000`）并跳转执行。

APP 同时支持三种 OTA 升级通路：

- **串口 UART**：Bootloader 端接收，APP 不直接参与
- **以太网 TCP**：APP 运行 lwIP TCP 服务器（端口 `6000`），接收固件写入非活跃 A/B 槽位
- **WiFi ESP8266**：通过 UART3 AT 透传模式，连接上位机 TCP 服务器下载固件

## 2. 运行环境

| 项目 | 说明 |
|------|------|
| 主控芯片 | STM32F407ZGT6（1MB Flash, 192KB SRAM） |
| 操作系统 | FreeRTOS v10.x |
| 网络协议栈 | lwIP v1.4.1 |
| 以太网 PHY | LAN8742A（RMII 接口） |
| WiFi 模块 | ESP8266EX（UART3, AT 指令, 透传模式） |
| 编译环境 | Keil MDK-ARM v5（ARM Compiler 5） |
| 链接地址 | `0x08010000`（运行区起始） |

## 3. Flash 分区布局

```
0x08000000 ┌──────────────┐
           │  Bootloader  │  48KB (Sector 0-2)
0x0800C000 ├──────────────┤
           │  参数/标志区  │  16KB (Sector 3)
0x08010000 ├──────────────┤
           │   运行区     │  192KB (Sector 4-5)  ← APP链接地址
0x08040000 ├──────────────┤
           │   槽位 A     │  256KB (Sector 6-7)
0x08080000 ├──────────────┤
           │   槽位 B     │  256KB (Sector 8-9)
0x080C0000 └──────────────┘
```

> 布局定义统一在 [iap_layout.h](USER/App/iap_layout.h)，与 Bootloader 端完全一致。

## 4. 任务架构

```
main()
  └─ AppStartTask (优先级5)
       ├─ ETH_BSP_Config()        // 以太网硬件初始化
       ├─ LwIP_Init()             // lwIP协议栈初始化
       ├─ EthernetPollTask (优先级4)  // 以太网轮询
       ├─ PrintTask1 (优先级2)    // "任务1 运行中" 每秒打印
       ├─ PrintTask2 (优先级2)    // "任务2 运行中" 每1.5秒打印
       ├─ OTA_UpdateTask (优先级3)    // TCP OTA服务器 :6000
       └─ IAP_WIFI_Init() + wifi_ota任务 (优先级3)  // WiFi OTA客户端
```

## 5. 关键模块说明

### 5.1 以太网 OTA（[ota_update.c](USER/App/ota_update.c)）

- 创建 TCP 服务器监听端口 `6000`
- 接收上位机 TCP 客户端发来的 IAP 帧（同串口帧协议）
- 解析帧 → 按 256 字节分片写入非活跃 A/B 槽位
- 空闲超时 `1500ms` 后校验向量表 → 写标志区 → 软复位
- 交由 Bootloader 完成固件拷贝

### 5.2 WiFi OTA（[iap_wifi.c](USER/App/iap_wifi.c)）

- 硬件：ESP8266 连接 UART3（PB10/PB11），控制引脚 PG15(RST)、PE2(EN)
- 初始化序列：AT 测试 → DHCP → STA 模式 → 连接 AP → TCP 连接上位机 → 透传模式
- 在透传模式下接收上位机发送的 IAP 帧
- 解析帧 → Flash 写入非活跃槽位
- 空闲超时 `1500ms` 后写标志 → 软复位
- 支持 TCP 断开自动重连

### 5.3 IAP 帧协议

与 Bootloader 共用统一的帧格式：

```
帧头(2B)  命令(2B)  负载长度N(2B,BE)  负载(N字节)  保留(4B,0)  CRC16(2B,LE)
0x5A 0xA5  [CMD_H CMD_L]  [N_H N_L]  [Payload...]  [0 0 0 0]  [CRC_L CRC_H]
```

命令字：

| 命令 | 编码 | 含义 |
|------|------|------|
| `CMD_FIRMWARE` | `0x0001` | 固件数据帧，负载≤256字节 |
| `CMD_ENTER_IAP` | `0x0002` | 进入IAP模式，N=0 |

### 5.4 网络配置（[netconf.c](USER/App/netconf.c)）

- 默认使用静态 IP：定义在 `netconf.h` 中
- lwIP 初始化流程：`tcpip_init → netif_add → netif_set_default`
- 以太网链路状态检测与回调

### 5.5 中断服务（[stm32f4xx_it.c](USER/stm32f4xx_it.c)）

- `SysTick_Handler`：FreeRTOS 系统时钟
- `USART3_IRQHandler`：WiFi 模块数据接收（RXNE 逐字节 + IDLE 帧结束检测）
- `HardFault_Handler`：硬件错误打印

## 6. 编译与烧录

### 6.1 Keil 编译

1. 打开 `Project/RVMDK(uv5)/BH-F407.uvprojx`
2. 选择 Target：`IAP_APP_V3`
3. Rebuild 编译
4. 输出文件：`Output/IAP_APP_V3.axf`、`Output/IAP_APP_V3.bin`

### 6.2 烧录要点

- **APP 必须烧录到运行区，不可覆盖 Bootloader**
- 使用 IAP 升级时，上位机发送 `.bin` 文件，由 Bootloader 自动写入
- 首次烧录可使用 J-Link / ST-Link 直接烧录 Bootloader + APP

## 7. 运行现象

- 串口（UART1）打印启动信息、IP地址
- `任务1 运行中` 每 1 秒打印一次
- `任务2 运行中` 每 1.5 秒打印一次
- 以太网 TCP 服务器监听 6000 端口
- WiFi 模块启动后自动连接热点和上位机

## 8. 待完成事项

- [ ] 以太网 OTA：Qt 工具需增加 TCP 客户端模式（当前仅有 TCP 服务器，给 WiFi 用）
- [ ] 全包 CRC 校验：当前仅有逐帧 CRC16，缺少整包完整性校验
- [ ] 标志区版本号递增：当前固定写 `version=1`

## 9. 文件结构

```
IAP_APP_V3/
├── USER/
│   ├── main.c                    # 主程序入口 + 任务创建
│   ├── stm32f4xx_it.c/h         # 中断服务（含USART3 IRQ）
│   ├── FreeRTOSConfig.h          # FreeRTOS 配置
│   ├── App/
│   │   ├── iap_layout.h          # Flash 分区布局定义
│   │   ├── ota_update.c/h        # 以太网 TCP OTA 任务
│   │   ├── iap_wifi.c/h          # WiFi ESP8266 OTA 驱动
│   │   └── netconf.c/h           # lwIP 网络配置
│   ├── Bsp/
│   │   ├── LAN8742A/             # 以太网 PHY 驱动
│   │   └── usart/                # 调试串口驱动
│   └── lwip-1.4.1/               # lwIP 协议栈
├── FreeRTOS/                     # FreeRTOS 内核
├── Libraries/                    # STM32 标准外设库 + CMSIS
└── Project/RVMDK(uv5)/          # Keil 工程文件
```
