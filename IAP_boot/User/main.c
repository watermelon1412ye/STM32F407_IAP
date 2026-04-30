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
      printf("\r\nIAP: 接收完毕(空闲>1.5秒)，写入标志并复位...\r\n");
      if (IAP_UART_FinalizeDownload() != FLASH_COMPLETE)
      {
        printf("IAP致命错误: 固件收尾失败。\r\n");
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
  printf("=   IAP 引导程序 (5分区状态机)         =\r\n");
  printf("========================================\r\n");

  IAP_UART_SetFlashProgramAllowed(0U);

  while (wait_left > 0U)
  {
    uint8_t slice;

    printf("倒计时 %u 秒: 发送 ENTER_IAP(0x0002) 或等待 A/B 启动...\r\n",
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

  printf("\r\n超时: 从标志区/槽位A/B 加载到运行区，然后跳转。\r\n");
  IAP_Boot_NormalFromFlag();

enter_permanent_iap:
  printf("\r\n进入永久串口 IAP 模式 (写入非活跃槽位，然后复位)。\r\n");
  IAP_UART_ResetSession();

  FLASH_Unlock();
  if (IAP_UART_PrepareDownloadSlot() != FLASH_COMPLETE)
  {
    printf("IAP致命错误: 准备非活跃槽位失败。\r\n");
    FLASH_Lock();
    while (1) {}
  }
  printf("IAP: 非活跃槽位已擦除，等待固件 (命令 0x0001)...\r\n");

  IAP_UART_SetFlashProgramAllowed(1U);
  IAP_RunSerialIapLoop();
}
