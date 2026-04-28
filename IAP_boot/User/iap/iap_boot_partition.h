#ifndef IAP_BOOT_PARTITION_H
#define IAP_BOOT_PARTITION_H

#include <stdint.h>
#include "stm32f4xx_flash.h"

#define IAP_FLAG_MAGIC_VAL  0x424F4F54u

typedef struct __attribute__((packed)) IapFlagBlock_s {
  uint32_t magic;
  uint32_t version;
  uint32_t boot_from;
  uint32_t image_size;
} IapFlagBlock_t;

uint8_t IAP_Flag_Read(IapFlagBlock_t *flag_out);
uint32_t IAP_SlotStartFromBootFrom(uint32_t boot_from);
FLASH_Status IAP_PrepareInactiveSlot(uint32_t *slot_start_out, uint32_t *boot_from_out);
FLASH_Status IAP_WriteBootFlag(uint32_t boot_from, uint32_t image_size);

void IAP_Flag_GetCopySource(uint32_t *src_out, uint32_t *len_out);
FLASH_Status IAP_EraseRunRegion(void);
FLASH_Status IAP_CopyToRun(uint32_t src, uint32_t len);
void IAP_Boot_NormalFromFlag(void);

#endif
