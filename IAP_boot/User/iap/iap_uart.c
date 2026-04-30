#include "iap_uart.h"
#include "iap_boot_partition.h"
#include "stm32f4xx.h"
#include "stm32f4xx_flash.h"
#include <stdio.h>
#include <string.h>

#define HDR0  0x5AU
#define HDR1  0xA5U

uint8_t rx_buffer[IAP_UART_RX_BUFFER_SIZE];
uint32_t flash_write_addr = 0U;
volatile uint8_t g_iap_enter_requested;

static uint8_t s_flash_program_allowed;
static uint32_t s_download_slot_start;
static uint32_t s_download_boot_from;
static uint32_t s_download_size;
static uint8_t s_download_prepared;
static uint8_t s_received_firmware;

static uint8_t s_snap[IAP_UART_RX_BUFFER_SIZE];
static volatile uint16_t s_rx_len;
static volatile uint16_t s_snap_len;
static volatile uint16_t s_idle_ms;
static volatile uint8_t  s_frame_ready;

static volatile uint32_t s_sys_ms;
static volatile uint32_t s_last_rx_ms;

static void snapshot_rx_locked(void)
{
  uint16_t len = s_rx_len;

  if (len == 0U || len > IAP_UART_RX_BUFFER_SIZE)
  {
    s_rx_len = 0U;
    s_idle_ms = 0U;
    return;
  }

  memcpy(s_snap, (const void *)rx_buffer, len);
  s_snap_len = len;
  s_rx_len = 0U;
  s_idle_ms = 0U;
  s_frame_ready = 1U;
}

static uint8_t IAP_ImageVectorValid(uint32_t vector_addr, uint32_t code_start, uint32_t code_end)
{
  uint32_t app_msp = *(volatile uint32_t *)vector_addr;
  uint32_t app_reset = *(volatile uint32_t *)(vector_addr + 4U);

  if (app_msp == 0xFFFFFFFFUL || app_reset == 0xFFFFFFFFUL)
  {
    return 0U;
  }
  if (app_msp < 0x20000000UL || app_msp > 0x2001FFFFUL)
  {
    return 0U;
  }
  if ((app_reset & 1UL) == 0UL)
  {
    return 0U;
  }
  if (app_reset < code_start || app_reset >= code_end)
  {
    return 0U;
  }
  return 1U;
}

void IAP_UART_Init(void)
{
  s_rx_len = 0U;
  s_snap_len = 0U;
  s_idle_ms = 0U;
  s_frame_ready = 0U;

  s_sys_ms = 0U;
  s_last_rx_ms = 0xFFFFFFFFUL;

  memset(rx_buffer, 0, sizeof(rx_buffer));
  memset(s_snap, 0, sizeof(s_snap));

  g_iap_enter_requested = 0U;
  s_flash_program_allowed = 0U;
  s_download_slot_start = 0U;
  s_download_boot_from = 0U;
  s_download_size = 0U;
  s_download_prepared = 0U;
  s_received_firmware = 0U;
  flash_write_addr = 0U;

  (void)SysTick_Config(SystemCoreClock / 1000U);
}

void IAP_UART_ResetSession(void)
{
  __disable_irq();
  s_rx_len = 0U;
  s_snap_len = 0U;
  s_idle_ms = 0U;
  s_frame_ready = 0U;
  memset(rx_buffer, 0, sizeof(rx_buffer));
  memset(s_snap, 0, sizeof(s_snap));
  s_last_rx_ms = 0xFFFFFFFFUL;
  __enable_irq();

  g_iap_enter_requested = 0U;
  s_flash_program_allowed = 0U;
  s_download_slot_start = 0U;
  s_download_boot_from = 0U;
  s_download_size = 0U;
  s_download_prepared = 0U;
  s_received_firmware = 0U;
  flash_write_addr = 0U;
}

void IAP_UART_SetFlashProgramAllowed(uint8_t allow)
{
  s_flash_program_allowed = (allow != 0U) ? 1U : 0U;
}

FLASH_Status IAP_UART_PrepareDownloadSlot(void)
{
  FLASH_Status st;

  s_download_size = 0U;
  s_received_firmware = 0U;
  flash_write_addr = 0U;
  s_download_prepared = 0U;

  st = IAP_PrepareInactiveSlot(&s_download_slot_start, &s_download_boot_from);
  if (st != FLASH_COMPLETE)
  {
    return st;
  }

  flash_write_addr = s_download_slot_start;
  s_download_prepared = 1U;

  printf("IAP: 串口目标槽位 %c, 写入起始地址=0x%08lX\r\n",
         (s_download_boot_from == 1U) ? 'B' : 'A',
         (unsigned long)s_download_slot_start);
  return FLASH_COMPLETE;
}

uint8_t IAP_UART_HasReceivedFirmware(void)
{
  return s_received_firmware;
}

void IAP_UART_IrqRxByte(uint8_t byte)
{
  uint16_t len = s_rx_len;

  s_idle_ms = 0U;
  s_last_rx_ms = s_sys_ms;

  if (len >= IAP_UART_RX_BUFFER_SIZE)
  {
    printf("IAP警告: 接收缓冲区溢出，丢弃当前帧。\r\n");
    s_rx_len = 0U;
    return;
  }

  rx_buffer[len] = byte;
  s_rx_len = (uint16_t)(len + 1U);
}

void IAP_UART_OnRxOverflow(void)
{
  printf("IAP警告: 串口溢出错误，重置接收帧。\r\n");
  s_rx_len = 0U;
  s_idle_ms = 0U;
}

void IAP_UART_OnSysTick1ms(void)
{
  s_sys_ms++;

  if (s_rx_len == 0U)
  {
    s_idle_ms = 0U;
    return;
  }

  s_idle_ms++;

  if (s_idle_ms < IAP_IDLE_TIMEOUT_MS)
  {
    return;
  }

  if (s_frame_ready != 0U)
  {
    return;
  }

  __disable_irq();
  snapshot_rx_locked();
  __enable_irq();
}

uint8_t IAP_UART_FrameReady(void)
{
  return s_frame_ready;
}

uint32_t IAP_UART_GetIdleMs(void)
{
  uint32_t now;
  uint32_t last;

  __disable_irq();
  now = s_sys_ms;
  last = s_last_rx_ms;
  __enable_irq();

  if (last == 0xFFFFFFFFUL)
  {
    return 0xFFFFFFFFUL;
  }

  return now - last;
}

uint16_t CRC16_Modbus(const uint8_t *data, uint32_t len)
{
  uint16_t crc = 0xFFFFU;
  uint32_t i;
  int bit;

  if (data == NULL || len == 0U)
  {
    return crc;
  }

  for (i = 0U; i < len; i++)
  {
    crc ^= data[i];
    for (bit = 0; bit < 8; bit++)
    {
      if ((crc & 1U) != 0U)
      {
        crc = (uint16_t)((crc >> 1) ^ 0xA001U);
      }
      else
      {
        crc = (uint16_t)(crc >> 1);
      }
    }
  }
  return crc;
}

static FLASH_Status IAP_FlashClearAndProgramWord(uint32_t addr, uint32_t data)
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

static FLASH_Status IAP_FlashClearAndProgramByte(uint32_t addr, uint8_t data)
{
  FLASH_Status st;

  (void)FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

  st = FLASH_ProgramByte(addr, data);
  if (st != FLASH_COMPLETE)
  {
    (void)FLASH_WaitForLastOperation();
    return st;
  }

  return FLASH_WaitForLastOperation();
}

static FLASH_Status IAP_FlashWriteBytes(const uint8_t *data, uint16_t len)
{
  uint16_t idx = 0U;
  uint32_t addr = flash_write_addr;
  uint32_t slot_end;

  if (data == NULL || len == 0U)
  {
    return FLASH_COMPLETE;
  }

  if (s_download_prepared == 0U)
  {
    printf("IAP错误: 下载槽位未准备。\r\n");
    return FLASH_ERROR_OPERATION;
  }

  slot_end = s_download_slot_start + IAP_RUN_SIZE;
  if (addr < s_download_slot_start)
  {
    addr = s_download_slot_start;
  }

  if (addr + (uint32_t)len > slot_end)
  {
    printf("IAP错误: 槽位写入超出范围 (地址=0x%08lX, 长度=%u)\r\n",
           (unsigned long)addr,
           (unsigned)len);
    return FLASH_ERROR_OPERATION;
  }

  while (idx < len)
  {
    uint32_t cur_addr = addr;
    FLASH_Status st;

    if (((cur_addr & 0x3U) == 0U) && (idx + 4U <= len))
    {
      uint32_t w =
        ((uint32_t)data[idx + 0U]) |
        ((uint32_t)data[idx + 1U] << 8) |
        ((uint32_t)data[idx + 2U] << 16) |
        ((uint32_t)data[idx + 3U] << 24);

      st = IAP_FlashClearAndProgramWord(cur_addr, w);
      if (st != FLASH_COMPLETE)
      {
        printf("IAP错误: ProgramWord失败 地址=0x%08lX 状态=%d\r\n",
               (unsigned long)cur_addr,
               (int)st);
        return st;
      }

      addr += 4U;
      idx += 4U;
      continue;
    }

    st = IAP_FlashClearAndProgramByte(cur_addr, data[idx]);
    if (st != FLASH_COMPLETE)
    {
      printf("IAP错误: ProgramByte失败 地址=0x%08lX 状态=%d\r\n",
             (unsigned long)cur_addr,
             (int)st);
      return st;
    }

    addr += 1U;
    idx += 1U;
  }

  flash_write_addr = addr;
  s_download_size += len;
  s_received_firmware = 1U;
  return FLASH_COMPLETE;
}

static uint8_t IAP_AppVectorValid(uint32_t app_addr)
{
  return IAP_ImageVectorValid(app_addr, app_addr, app_addr + IAP_RUN_SIZE);
}

FLASH_Status IAP_UART_FinalizeDownload(void)
{
  if (s_download_prepared == 0U || s_received_firmware == 0U || s_download_size == 0U)
  {
    printf("IAP错误: 未接收固件，跳过收尾。\r\n");
    return FLASH_ERROR_OPERATION;
  }

  if (IAP_ImageVectorValid(s_download_slot_start, IAP_RUN_START, IAP_RUN_END) == 0U)
  {
    printf("IAP错误: 下载的固件向量表无效。\r\n");
    return FLASH_ERROR_OPERATION;
  }

  printf("IAP: 槽位%c收尾完成, 固件大小=%lu\r\n",
         (s_download_boot_from == 1U) ? 'B' : 'A',
         (unsigned long)s_download_size);
  return IAP_WriteBootFlag(s_download_boot_from, s_download_size);
}

void IAP_Load_App(uint32_t app_addr)
{
  typedef void (*pFunction)(void);
  uint32_t i;
  pFunction reset_handler;
  uint32_t app_msp = *(volatile uint32_t *)app_addr;
  uint32_t app_reset = *(volatile uint32_t *)(app_addr + 4U);

  if (IAP_AppVectorValid(app_addr) == 0U)
  {
    printf("IAP错误: APP向量表无效，拒绝跳转。MSP=0x%08lX RESET=0x%08lX\r\n",
           (unsigned long)app_msp,
           (unsigned long)app_reset);
    while (1) {}
  }

  __disable_irq();
  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL  = 0U;
  for (i = 0U; i < 8U; i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFFUL;
    NVIC->ICPR[i] = 0xFFFFFFFFUL;
  }
  SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
  __DSB();
  __ISB();
  SCB->VTOR = app_addr;
  __set_PSP(0U);
  __set_CONTROL(0U);
  __set_MSP(app_msp);
  __DSB();
  __ISB();

  /* Re-open global IRQs before handing control to the application reset handler. */
  __enable_irq();

  reset_handler = (pFunction)app_reset;
  reset_handler();

  while (1)
  {
  }
}

void Parse_IAP_Frame(void)
{
  uint8_t local[IAP_UART_RX_BUFFER_SIZE];
  uint16_t n;
  uint16_t i;
  uint8_t saw_header = 0U;

  if (s_frame_ready == 0U)
  {
    return;
  }

  __disable_irq();
  n = s_snap_len;
  if (n > IAP_UART_RX_BUFFER_SIZE)
  {
    n = IAP_UART_RX_BUFFER_SIZE;
  }
  memcpy(local, s_snap, n);
  s_frame_ready = 0U;
  __enable_irq();

  i = 0U;
  while ((uint32_t)i + (uint32_t)IAP_FRAME_OVERHEAD <= (uint32_t)n)
  {
    if (local[i] != HDR0 || local[i + 1U] != HDR1)
    {
      i++;
      continue;
    }

    saw_header = 1U;

    if ((uint32_t)i + 6U > (uint32_t)n)
    {
      printf("IAP错误: 长度不足以读取N字段\r\n");
      break;
    }

    {
      uint8_t cmd_h = local[i + 2U];
      uint8_t cmd_l = local[i + 3U];
      uint8_t cmd_enter =
        (cmd_h == IAP_CMD_ENTER_IAP_H && cmd_l == IAP_CMD_ENTER_IAP_L) ? 1U : 0U;
      uint8_t cmd_firmware =
        (cmd_h == IAP_CMD_FIRMWARE_H && cmd_l == IAP_CMD_FIRMWARE_L) ? 1U : 0U;

      if (cmd_firmware == 0U && cmd_enter == 0U)
      {
        printf("IAP错误: 未知命令 0x%02X 0x%02X\r\n", (unsigned)cmd_h, (unsigned)cmd_l);
        i++;
        continue;
      }

      {
        uint16_t N = (uint16_t)(((uint16_t)local[i + 4U] << 8) | local[i + 5U]);
        uint32_t frame_total;
        uint32_t crc_off;
        uint16_t crc_calc;
        uint16_t crc_rx;
        uint32_t k;
        uint8_t skip_one = 0U;

        if (N > IAP_FRAME_MAX_N)
        {
          printf("IAP错误: 无效的N=%u\r\n", (unsigned)N);
          i++;
          continue;
        }

        if (cmd_enter != 0U && N != 0U)
        {
          printf("IAP错误: ENTER_IAP命令要求N=0\r\n");
          i++;
          continue;
        }

        frame_total = (uint32_t)N + (uint32_t)IAP_FRAME_OVERHEAD;
        if ((uint32_t)i + frame_total > (uint32_t)n)
        {
          printf("IAP错误: 长度不匹配 (需要%lu字节, 剩余%u)\r\n",
                 (unsigned long)frame_total,
                 (unsigned)((uint32_t)n - (uint32_t)i));
          break;
        }

        crc_off = (uint32_t)i + 2U + 2U + 2U + (uint32_t)N + 4U;
        for (k = 0U; k < 4U; k++)
        {
          if (local[i + 6U + N + k] != 0U)
          {
            printf("IAP错误: 保留字节非零\r\n");
            skip_one = 1U;
            break;
          }
        }

        if (skip_one != 0U)
        {
          i++;
          continue;
        }

        crc_calc = CRC16_Modbus(local + i, (uint32_t)(10U + N));
        crc_rx = (uint16_t)local[crc_off] |
                 (uint16_t)((uint16_t)local[crc_off + 1U] << 8);

        if (crc_calc != crc_rx)
        {
          printf("IAP错误: CRC校验失败 (计算=0x%04X, 接收=0x%04X)\r\n",
                 crc_calc,
                 crc_rx);
        }
        else if (cmd_enter != 0U)
        {
          printf("上位机 ENTER_IAP 命令已确认。\r\n");
          g_iap_enter_requested = 1U;
        }
        else if (cmd_firmware != 0U)
        {
          if (s_flash_program_allowed == 0U)
          {
            printf("IAP: 固件帧已忽略 (请先发送 ENTER_IAP 0x0002)。\r\n");
          }
          else if (s_download_prepared == 0U)
          {
            printf("IAP错误: 下载槽位未准备。\r\n");
          }
          else
          {
            FLASH_Status wst;

            printf("固件包接收成功。长度=%u, CRC校验通过\r\n", (unsigned)N);
            wst = IAP_FlashWriteBytes(local + i + 6U, N);
            if (wst != FLASH_COMPLETE)
            {
              printf("IAP错误: Flash编程失败 状态=%d\r\n", (int)wst);
              return;
            }

            printf("IAP烧写进度: 地址=0x%08lX, 已写入=%lu字节\r\n",
                   (unsigned long)flash_write_addr,
                   (unsigned long)(flash_write_addr - s_download_slot_start));
          }
        }
        else
        {
          printf("IAP错误: 内部命令路由异常\r\n");
        }

        i = (uint16_t)((uint32_t)i + frame_total);
      }
    }
  }

  if (saw_header == 0U && n > 0U)
  {
    printf("IAP错误: 未找到帧同步头 (0x5A 0xA5)\r\n");
  }

  memset(local, 0, sizeof(local));
}
