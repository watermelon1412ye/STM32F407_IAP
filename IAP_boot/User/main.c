#include "stm32f4xx.h"
#include "stm32f4xx_flash.h"
#include "./led/bsp_led.h"
#include "./key/bsp_key.h"
#include "bsp_debug_usart.h"
#include "./iap/iap_uart.h"
#include "./iap/iap_boot_partition.h"

static void Delay_ms(uint32_t ms)
{
  uint32_t i;
  uint32_t j;

  for (i = 0; i < ms; i++)
  {
    for (j = 0; j < 10000; j++)
    {
    }
  }
}

static void IAP_RunSerialIapLoop(void)
{
  uint8_t finalize_done = 0U;

  for (;;)
  {
    if (IAP_UART_FrameReady())
    {
      Parse_IAP_Frame();
    }

    if ((finalize_done == 0U) &&
        (IAP_UART_HasReceivedFirmware() != 0U) &&
        (IAP_UART_GetIdleMs() >= IAP_JUMP_IDLE_TIMEOUT_MS) &&
        (IAP_UART_FrameReady() == 0U))
    {
      finalize_done = 1U;
      printf("\r\nIAP: receive done (idle>1.5s), finalize flag and reset...\r\n");
      if (IAP_UART_FinalizeDownload() != FLASH_COMPLETE)
      {
        printf("IAP fatal: finalize download failed.\r\n");
        FLASH_Lock();
        while (1) {}
      }
      FLASH_Lock();
      NVIC_SystemReset();
    }
  }
}

int main(void)
{
  const uint8_t kWaitSeconds = 3U;
  uint8_t wait_left = kWaitSeconds;

  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

  LED_GPIO_Config();
  Key_GPIO_Config();
  Debug_USART_Config();
  IAP_UART_Init();

  printf("\r\n========================================\r\n");
  printf("=   IAP Bootloader (5-zone FSM)         =\r\n");
  printf("========================================\r\n");

  IAP_UART_SetFlashProgramAllowed(0U);

  while (wait_left > 0U)
  {
    uint8_t slice;

    printf("Countdown %u s: send ENTER_IAP(0x0002) or wait for A/B boot...\r\n",
           (unsigned)wait_left);
    LED_YELLOW;

    for (slice = 0U; slice < 10U; slice++)
    {
      Delay_ms(100);
      if (IAP_UART_FrameReady())
      {
        Parse_IAP_Frame();
      }
      if (g_iap_enter_requested != 0U)
      {
        goto enter_permanent_iap;
      }
    }
    wait_left--;
  }

  printf("\r\nTimeout: load from flag / slot A/B -> run, then jump.\r\n");
  IAP_Boot_NormalFromFlag();

enter_permanent_iap:
  printf("\r\nEnter permanent UART IAP (write inactive slot, then reset).\r\n");
  IAP_UART_ResetSession();

  FLASH_Unlock();
  if (IAP_UART_PrepareDownloadSlot() != FLASH_COMPLETE)
  {
    printf("IAP fatal: prepare inactive slot failed.\r\n");
    FLASH_Lock();
    while (1) {}
  }
  printf("IAP: inactive slot erased. Waiting for firmware (cmd 0x0001)...\r\n");

  IAP_UART_SetFlashProgramAllowed(1U);
  IAP_RunSerialIapLoop();
}
