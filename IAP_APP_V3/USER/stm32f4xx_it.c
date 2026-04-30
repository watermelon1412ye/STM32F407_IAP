#include "stm32f4xx_it.h"
#include "./Bsp/usart/bsp_debug_usart.h"
#include "App/iap_wifi.h"
#include "FreeRTOS.h"
#include "task.h"

extern void xPortSysTickHandler(void);

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
  printf("硬件错误!!!\r\n");
  while (1) {
  }
}

void MemManage_Handler(void)
{
  while (1) {
  }
}

void BusFault_Handler(void)
{
  while (1) {
  }
}

void UsageFault_Handler(void)
{
  while (1) {
  }
}

void DebugMon_Handler(void)
{
}

void USART3_IRQHandler(void)
{
  if (USART_GetITStatus(USART3, USART_IT_RXNE) != RESET) {
    IAP_WIFI_IrqRxByte((uint8_t)USART_ReceiveData(USART3));
    USART_ClearITPendingBit(USART3, USART_IT_RXNE);
  }

  if (USART_GetITStatus(USART3, USART_IT_IDLE) != RESET) {
    (void)USART_ReceiveData(USART3);
    USART_ClearITPendingBit(USART3, USART_IT_IDLE);
    IAP_WIFI_IrqOnIdle();
  }
}

void SysTick_Handler(void)
{
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
    xPortSysTickHandler();
  }
}
