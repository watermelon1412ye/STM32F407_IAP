#include "iap_boot_partition.h"
#include "iap_layout.h"
#include "iap_uart.h"
#include <stdio.h>

static uint8_t IAP_RunVectorLooksValid(void)
{
  uint32_t sp = *(volatile uint32_t *)IAP_RUN_START;
  uint32_t reset = *(volatile uint32_t *)(IAP_RUN_START + 4U);

  if (sp == 0xFFFFFFFFU || reset == 0xFFFFFFFFU)
  {
    return 0U;
  }
  if (sp < 0x20000000U || sp > 0x2001FFFFU)
  {
    return 0U;
  }
  if ((reset & 1U) == 0U)
  {
    return 0U;
  }
  if (reset < IAP_RUN_START || reset >= IAP_RUN_END)
  {
    return 0U;
  }
  return 1U;
}

static FLASH_Status ClearFlagsAndProgramWord(uint32_t addr, uint32_t data)
{
  FLASH_Status st;

  (void)FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

  st = FLASH_ProgramWord(addr, data);
  if (st != FLASH_COMPLETE)
  {
    (void)FLASH_WaitForLastOperation();
    return st;
  }

  return FLASH_WaitForLastOperation();
}

static FLASH_Status IAP_EraseOneSector(uint32_t sector)
{
  FLASH_Status st;

  st = FLASH_EraseSector(sector, VoltageRange_3);
  if (st != FLASH_COMPLETE)
  {
    printf("IAP error: erase sector %lu failed st=%d\r\n",
           (unsigned long)sector,
           (int)st);
  }
  return st;
}

static FLASH_Status IAP_EraseFlagSector(void)
{
  return IAP_EraseOneSector(FLASH_Sector_3);
}

static FLASH_Status IAP_EraseSlotRegion(uint32_t boot_from)
{
  FLASH_Status st;
  uint32_t sector_a;
  uint32_t sector_b;

  if (boot_from == 1U)
  {
    sector_a = FLASH_Sector_8;
    sector_b = FLASH_Sector_9;
  }
  else
  {
    sector_a = FLASH_Sector_6;
    sector_b = FLASH_Sector_7;
  }

  st = IAP_EraseOneSector(sector_a);
  if (st != FLASH_COMPLETE)
  {
    return st;
  }

  st = IAP_EraseOneSector(sector_b);
  if (st != FLASH_COMPLETE)
  {
    return st;
  }

  return FLASH_COMPLETE;
}

uint8_t IAP_Flag_Read(IapFlagBlock_t *flag_out)
{
  const IapFlagBlock_t *flag = (const IapFlagBlock_t *)IAP_FLAG_SECTOR_START;

  if (flag_out != NULL)
  {
    *flag_out = *flag;
  }

  return (flag->magic == IAP_FLAG_MAGIC_VAL) ? 1U : 0U;
}

uint32_t IAP_SlotStartFromBootFrom(uint32_t boot_from)
{
  return (boot_from == 1U) ? IAP_SLOT_B_START : IAP_SLOT_A_START;
}

FLASH_Status IAP_PrepareInactiveSlot(uint32_t *slot_start_out, uint32_t *boot_from_out)
{
  IapFlagBlock_t flag;
  uint32_t next_boot_from = 1U;
  uint32_t slot_start;
  FLASH_Status st;

  if (IAP_Flag_Read(&flag) != 0U)
  {
    next_boot_from = (flag.boot_from == 1U) ? 0U : 1U;
  }

  slot_start = IAP_SlotStartFromBootFrom(next_boot_from);
  printf("IAP: prepare inactive slot %c @0x%08lX\r\n",
         (next_boot_from == 1U) ? 'B' : 'A',
         (unsigned long)slot_start);

  st = IAP_EraseSlotRegion(next_boot_from);
  if (st != FLASH_COMPLETE)
  {
    return st;
  }

  if (slot_start_out != NULL)
  {
    *slot_start_out = slot_start;
  }
  if (boot_from_out != NULL)
  {
    *boot_from_out = next_boot_from;
  }

  return FLASH_COMPLETE;
}

FLASH_Status IAP_WriteBootFlag(uint32_t boot_from, uint32_t image_size)
{
  IapFlagBlock_t old_flag;
  IapFlagBlock_t new_flag;
  FLASH_Status st;

  if (image_size == 0U || image_size > IAP_RUN_SIZE)
  {
    printf("IAP error: invalid image size %lu\r\n", (unsigned long)image_size);
    return FLASH_ERROR_OPERATION;
  }

  new_flag.magic = IAP_FLAG_MAGIC_VAL;
  new_flag.version = (IAP_Flag_Read(&old_flag) != 0U) ? (old_flag.version + 1U) : 1U;
  new_flag.boot_from = (boot_from == 1U) ? 1U : 0U;
  new_flag.image_size = image_size;

  st = IAP_EraseFlagSector();
  if (st != FLASH_COMPLETE)
  {
    return st;
  }

  st = ClearFlagsAndProgramWord(IAP_FLAG_SECTOR_START + 0U, new_flag.magic);
  if (st != FLASH_COMPLETE)
  {
    return st;
  }
  st = ClearFlagsAndProgramWord(IAP_FLAG_SECTOR_START + 4U, new_flag.version);
  if (st != FLASH_COMPLETE)
  {
    return st;
  }
  st = ClearFlagsAndProgramWord(IAP_FLAG_SECTOR_START + 8U, new_flag.boot_from);
  if (st != FLASH_COMPLETE)
  {
    return st;
  }
  st = ClearFlagsAndProgramWord(IAP_FLAG_SECTOR_START + 12U, new_flag.image_size);
  if (st != FLASH_COMPLETE)
  {
    return st;
  }

  printf("IAP: flag updated, version=%lu, boot_from=%lu, size=%lu\r\n",
         (unsigned long)new_flag.version,
         (unsigned long)new_flag.boot_from,
         (unsigned long)new_flag.image_size);
  return FLASH_COMPLETE;
}

void IAP_Flag_GetCopySource(uint32_t *src_out, uint32_t *len_out)
{
  IapFlagBlock_t flag;
  uint32_t len;

  if (src_out == NULL || len_out == NULL)
  {
    return;
  }

  if (IAP_Flag_Read(&flag) == 0U)
  {
    printf("Flag: magic invalid (0x%08lX).\r\n", (unsigned long)flag.magic);
    if (IAP_RunVectorLooksValid() != 0U)
    {
      printf("Flag: run region has valid vectors, will jump without A/B copy.\r\n");
      *src_out = IAP_RUN_START;
      *len_out = 0U;
      return;
    }
    printf("Flag: default copy from slot A.\r\n");
    *src_out = IAP_SLOT_A_START;
    *len_out = IAP_RUN_SIZE;
    return;
  }

  *src_out = IAP_SlotStartFromBootFrom(flag.boot_from);
  len = flag.image_size;
  if (len == 0U || len > IAP_RUN_SIZE)
  {
    len = IAP_RUN_SIZE;
  }

  len = (len + 3U) & ~3U;
  *len_out = len;
  printf("Flag: valid, boot_from=%lu, copy_len=%lu\r\n",
         (unsigned long)flag.boot_from,
         (unsigned long)len);
}

FLASH_Status IAP_EraseRunRegion(void)
{
  FLASH_Status st;

  st = FLASH_EraseSector(FLASH_Sector_4, VoltageRange_3);
  if (st != FLASH_COMPLETE)
  {
    printf("IAP error: Erase Sector4 failed st=%d\r\n", (int)st);
    return st;
  }

  st = FLASH_EraseSector(FLASH_Sector_5, VoltageRange_3);
  if (st != FLASH_COMPLETE)
  {
    printf("IAP error: Erase Sector5 failed st=%d\r\n", (int)st);
    return st;
  }

  return FLASH_COMPLETE;
}

FLASH_Status IAP_CopyToRun(uint32_t src, uint32_t len)
{
  uint32_t i;
  FLASH_Status st;

  if (len > IAP_RUN_SIZE)
  {
    len = IAP_RUN_SIZE;
  }
  len = (len + 3U) & ~3U;

  if (src != IAP_SLOT_A_START && src != IAP_SLOT_B_START)
  {
    printf("IAP error: copy src 0x%08lX not in A/B slot\r\n", (unsigned long)src);
    return FLASH_ERROR_OPERATION;
  }

  for (i = 0U; i < len; i += 4U)
  {
    uint32_t w = *(volatile uint32_t *)(src + i);
    st = ClearFlagsAndProgramWord(IAP_RUN_START + i, w);
    if (st != FLASH_COMPLETE)
    {
      printf("IAP error: Copy program word failed off=%lu st=%d\r\n",
             (unsigned long)i,
             (int)st);
      return st;
    }
  }

  return FLASH_COMPLETE;
}

void IAP_Boot_NormalFromFlag(void)
{
  uint32_t src;
  uint32_t len;

  IAP_Flag_GetCopySource(&src, &len);

  if (len == 0U && src == IAP_RUN_START)
  {
    printf("IAP: direct jump to run 0x%08lX\r\n", (unsigned long)IAP_RUN_START);
    IAP_Load_App(IAP_RUN_START);
  }

  FLASH_Unlock();

  if (IAP_EraseRunRegion() != FLASH_COMPLETE)
  {
    FLASH_Lock();
    printf("IAP fatal: cannot erase run region.\r\n");
    while (1) {}
  }

  if (IAP_CopyToRun(src, len) != FLASH_COMPLETE)
  {
    FLASH_Lock();
    printf("IAP fatal: copy A/B -> run failed.\r\n");
    while (1) {}
  }

  FLASH_Lock();
  printf("IAP: copy done, jumping to app 0x%08lX ...\r\n", (unsigned long)IAP_RUN_START);
  IAP_Load_App(IAP_RUN_START);
}
