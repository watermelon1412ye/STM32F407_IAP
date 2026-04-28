#include "lwip/opt.h"

#if !NO_SYS

#include "lwip/arch.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(xTimeInMs) \
  ( ( TickType_t ) ( ( ( TickType_t ) ( xTimeInMs ) * configTICK_RATE_HZ ) / 1000U ) )
#endif

static TickType_t sys_arch_ms_to_ticks(u32_t timeout_ms)
{
  TickType_t ticks;

  if (timeout_ms == 0U) {
    return 0;
  }

  ticks = pdMS_TO_TICKS(timeout_ms);
  if (ticks == 0U) {
    ticks = 1U;
  }

  return ticks;
}

void sys_init(void)
{
}

err_t sys_sem_new(sys_sem_t *sem, u8_t count)
{
  *sem = xSemaphoreCreateBinary();
  if (*sem == NULL) {
    return ERR_MEM;
  }

  if (count != 0U) {
    xSemaphoreGive(*sem);
  }

  return ERR_OK;
}

void sys_sem_free(sys_sem_t *sem)
{
  if ((sem != NULL) && (*sem != NULL)) {
    vSemaphoreDelete(*sem);
    *sem = NULL;
  }
}

void sys_sem_signal(sys_sem_t *sem)
{
  if ((sem != NULL) && (*sem != NULL)) {
    xSemaphoreGive(*sem);
  }
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout)
{
  TickType_t start_ticks;
  TickType_t timeout_ticks;
  BaseType_t result;

  if ((sem == NULL) || (*sem == NULL)) {
    return SYS_ARCH_TIMEOUT;
  }

  start_ticks = xTaskGetTickCount();
  if (timeout == 0U) {
    timeout_ticks = portMAX_DELAY;
  } else {
    timeout_ticks = sys_arch_ms_to_ticks(timeout);
  }
  result = xSemaphoreTake(*sem, timeout_ticks);

  if (result != pdTRUE) {
    return SYS_ARCH_TIMEOUT;
  }

  return (u32_t)((xTaskGetTickCount() - start_ticks) * portTICK_PERIOD_MS);
}

int sys_sem_valid(sys_sem_t *sem)
{
  return ((sem != NULL) && (*sem != NULL));
}

void sys_sem_set_invalid(sys_sem_t *sem)
{
  if (sem != NULL) {
    *sem = NULL;
  }
}

err_t sys_mbox_new(sys_mbox_t *mbox, int size)
{
  if (size <= 0) {
    size = 1;
  }

  *mbox = xQueueCreate((UBaseType_t)size, sizeof(void *));
  if (*mbox == NULL) {
    return ERR_MEM;
  }

  return ERR_OK;
}

void sys_mbox_free(sys_mbox_t *mbox)
{
  if ((mbox != NULL) && (*mbox != NULL)) {
    vQueueDelete(*mbox);
    *mbox = NULL;
  }
}

void sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
  void *message;

  if ((mbox == NULL) || (*mbox == NULL)) {
    return;
  }

  message = msg;
  while (xQueueSend(*mbox, &message, portMAX_DELAY) != pdPASS) {
  }
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
  void *message;

  if ((mbox == NULL) || (*mbox == NULL)) {
    return ERR_MEM;
  }

  message = msg;
  if (xQueueSend(*mbox, &message, 0) == pdPASS) {
    return ERR_OK;
  }

  return ERR_MEM;
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout)
{
  void *message;
  TickType_t start_ticks;
  TickType_t timeout_ticks;
  BaseType_t result;

  if ((mbox == NULL) || (*mbox == NULL)) {
    return SYS_ARCH_TIMEOUT;
  }

  start_ticks = xTaskGetTickCount();
  if (timeout == 0U) {
    timeout_ticks = portMAX_DELAY;
  } else {
    timeout_ticks = sys_arch_ms_to_ticks(timeout);
  }
  result = xQueueReceive(*mbox, &message, timeout_ticks);

  if (result != pdPASS) {
    if (msg != NULL) {
      *msg = NULL;
    }
    return SYS_ARCH_TIMEOUT;
  }

  if (msg != NULL) {
    *msg = message;
  }

  return (u32_t)((xTaskGetTickCount() - start_ticks) * portTICK_PERIOD_MS);
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
  void *message;

  if ((mbox == NULL) || (*mbox == NULL)) {
    return SYS_MBOX_EMPTY;
  }

  if (xQueueReceive(*mbox, &message, 0) != pdPASS) {
    if (msg != NULL) {
      *msg = NULL;
    }
    return SYS_MBOX_EMPTY;
  }

  if (msg != NULL) {
    *msg = message;
  }

  return 0;
}

int sys_mbox_valid(sys_mbox_t *mbox)
{
  return ((mbox != NULL) && (*mbox != NULL));
}

void sys_mbox_set_invalid(sys_mbox_t *mbox)
{
  if (mbox != NULL) {
    *mbox = NULL;
  }
}

sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg, int stacksize, int prio)
{
  xTaskHandle task_handle;
  uint16_t stack_words;

  if (stacksize <= 0) {
    stack_words = configMINIMAL_STACK_SIZE * 2U;
  } else {
    stack_words = (uint16_t)(stacksize / (int)sizeof(StackType_t));
    if (stack_words == 0U) {
      stack_words = configMINIMAL_STACK_SIZE;
    }
  }

  if (xTaskCreate(thread, name, stack_words, arg, (UBaseType_t)prio, &task_handle) != pdPASS) {
    return NULL;
  }

  return task_handle;
}

u32_t sys_jiffies(void)
{
  return (u32_t)xTaskGetTickCount();
}

u32_t sys_now(void)
{
  return (u32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

sys_prot_t sys_arch_protect(void)
{
  taskENTER_CRITICAL();
  return 1;
}

void sys_arch_unprotect(sys_prot_t pval)
{
  (void)pval;
  taskEXIT_CRITICAL();
}

#endif
