#include "stm32f4xx_it.h"
#include "./Bsp/usart/bsp_debug_usart.h"
#include "FreeRTOS.h"
#include "task.h"

extern void xPortSysTickHandler(void);

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
  printf("hardfault!!!\r\n");
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

void SysTick_Handler(void)
{
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
    xPortSysTickHandler();
  }
}
