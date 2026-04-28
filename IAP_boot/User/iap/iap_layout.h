/**
 * @file    iap_layout.h
 * @brief   STM32F407ZGTx 1MB 片内 Flash 五区布局（与 scatter 一致，勿随意改动）
 *
 *  | boot 48KB | 标志/参数 16KB | 运行区 192KB | A 槽 256KB | B 槽 256KB | 剩余保留 |
 *  运行区与 IAP_APP 工程链接起始地址必须一致（当前 0x08010000）。
 */
#ifndef IAP_LAYOUT_H
#define IAP_LAYOUT_H

#include <stdint.h>

/* --- Boot（与 IAP_boot scatter LR_IROM1 0x08000000 0xC000 一致）--- */
#define IAP_BOOT_REGION_START   0x08000000U
#define IAP_BOOT_REGION_SIZE    0x0000C000U

/* --- 标志/参数区：单独占用 Sector3（16KB），仅读写在明确 API 内进行 --- */
#define IAP_FLAG_SECTOR_START   0x0800C000U
#define IAP_FLAG_SECTOR_SIZE    0x00004000U

/* --- 运行区：Sector4(64K)+Sector5(128K) = 192KB --- */
#define IAP_RUN_START           0x08010000U
#define IAP_RUN_SIZE            0x00030000U
#define IAP_RUN_END             (IAP_RUN_START + IAP_RUN_SIZE)

/* --- A/B 备份槽：各 256KB（Sector6+7 / Sector8+9），有效镜像占前 IAP_RUN_SIZE --- */
#define IAP_SLOT_A_START        0x08040000U
#define IAP_SLOT_B_START        0x08080000U
#define IAP_SLOT_BANK_SIZE      0x00040000U

/* 与历史 iap_uart 命名兼容 */
#define IAP_APP_START_ADDR      IAP_RUN_START
#define IAP_APP_END_ADDR        IAP_RUN_END

#endif /* IAP_LAYOUT_H */
