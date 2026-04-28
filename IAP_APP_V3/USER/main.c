#include "stm32f4xx.h"
#include "./Bsp/usart/bsp_debug_usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "netconf.h"
#include "LAN8742A.h"
#include "ota_update.h"

#define APP_START_TASK_STACK_WORDS   256
#define ETH_POLL_TASK_STACK_WORDS    256
#define PRINT_TASK_STACK_WORDS       128
#define OTA_TASK_STACK_WORDS         512

static void AppStartTask(void *parameter);
static void EthernetPollTask(void *parameter);
static void PrintTask1(void *parameter);
static void PrintTask2(void *parameter);

int main(void)
{
  /* Boot jump path may arrive with PRIMASK still set; reopen IRQs for RTOS/lwIP. */
  __enable_irq();
  Debug_USART_Config();

  printf("\r\nIAP APP FreeRTOS + lwIP demo\r\n");
  printf("Static IP: %d.%d.%d.%d\r\n", IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);

  xTaskCreate(AppStartTask,
              "app_start",
              APP_START_TASK_STACK_WORDS,
              NULL,
              5,
              NULL);

  vTaskStartScheduler();

  while (1) {
  }
}

static void AppStartTask(void *parameter)
{
  (void)parameter;

  ETH_BSP_Config();
  printf("ETH BSP init done\r\n");

  LwIP_Init();
  printf("lwIP init done\r\n");

  xTaskCreate(EthernetPollTask,
              "eth_poll",
              ETH_POLL_TASK_STACK_WORDS,
              NULL,
              4,
              NULL);

  xTaskCreate(PrintTask1,
              "print1",
              PRINT_TASK_STACK_WORDS,
              NULL,
              2,
              NULL);

  xTaskCreate(PrintTask2,
              "print2",
              PRINT_TASK_STACK_WORDS,
              NULL,
              2,
              NULL);

  xTaskCreate(OTA_UpdateTask,
              "ota_tcp",
              OTA_TASK_STACK_WORDS,
              NULL,
              3,
              NULL);

  vTaskDelete(NULL);
}
 
static void EthernetPollTask(void *parameter)
{
  uint32_t local_time_ms;

  (void)parameter;

  for (;;) {
    while (ETH_CheckFrameReceived()) {
      LwIP_Pkt_Handle();
    }

    local_time_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    LwIP_Periodic_Handle(local_time_ms);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void PrintTask1(void *parameter)
{
  (void)parameter;

  for (;;) {
    printf("task1 alive\r\n");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

static void PrintTask2(void *parameter)
{
  (void)parameter;

  for (;;) {
    printf("task2 alive\r\n");
    vTaskDelay(pdMS_TO_TICKS(1500));
  }
}
