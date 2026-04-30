#include "iap_wifi.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "stm32f4xx.h"
#include "stm32f4xx_flash.h"
#include "iap_layout.h"
#include "FreeRTOS.h"
#include "task.h"

/* ================================================================
 *  UART3 接收缓冲区 (参考野火 bsp_esp8266 的 IDLE 中断方案)
 *
 *  RXNE: 逐字节存入线性缓冲区
 *  IDLE: 帧结束，置位 FramFinishFlag
 * ================================================================ */
#define WIFI_RX_BUF_MAX_LEN    2048U

static char             wifi_rx_buf[WIFI_RX_BUF_MAX_LEN];
static volatile uint16_t wifi_rx_len;
static volatile uint8_t  wifi_rx_finish_flag;
static volatile uint8_t  wifi_tcp_closed_flag;

/* 由 USART3 ISR 调用 */
void IAP_WIFI_IrqRxByte(uint8_t byte)
{
  uint16_t len = wifi_rx_len;

  if (len < (WIFI_RX_BUF_MAX_LEN - 1U)) {
    wifi_rx_buf[len] = (char)byte;
    wifi_rx_len = (uint16_t)(len + 1U);
  }
}

void IAP_WIFI_IrqOnIdle(void)
{
  wifi_rx_finish_flag = 1U;

  if (strstr(wifi_rx_buf, "CLOSED\r\n") != NULL) {
    wifi_tcp_closed_flag = 1U;
  }
}

static void wifi_rx_reset(void)
{
  __disable_irq();
  wifi_rx_len          = 0U;
  wifi_rx_finish_flag  = 0U;
  wifi_tcp_closed_flag = 0U;
  __enable_irq();
}

/* 等待 IDLE 帧完成，或超时 */
static uint8_t wifi_rx_wait_frame(uint32_t timeout_ms)
{
  uint32_t start = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

  while (wifi_rx_finish_flag == 0U) {
    uint32_t elapsed = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - start;
    if (elapsed >= timeout_ms) {
      return 0U;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return 1U;
}

/* ================================================================
 *  ESP8266 硬件控制 (与野火 bsp_esp8266 引脚一致)
 * ================================================================ */
static void wifi_hw_gpio_init(void)
{
  GPIO_InitTypeDef gpio;

  RCC_AHB1PeriphClockCmd(WIFI_EN_CLK | WIFI_RST_CLK, ENABLE);

  gpio.GPIO_Mode  = GPIO_Mode_OUT;
  gpio.GPIO_OType = GPIO_OType_PP;
  gpio.GPIO_Speed = GPIO_Speed_50MHz;
  gpio.GPIO_PuPd  = GPIO_PuPd_UP;

  gpio.GPIO_Pin = WIFI_EN_PIN;
  GPIO_Init(WIFI_EN_PORT, &gpio);

  gpio.GPIO_Pin = WIFI_RST_PIN;
  GPIO_Init(WIFI_RST_PORT, &gpio);

  GPIO_SetBits(WIFI_RST_PORT, WIFI_RST_PIN);
  GPIO_SetBits(WIFI_EN_PORT, WIFI_EN_PIN);
}

static void wifi_hw_reset(void)
{
  GPIO_ResetBits(WIFI_RST_PORT, WIFI_RST_PIN);
  vTaskDelay(pdMS_TO_TICKS(500));
  GPIO_SetBits(WIFI_RST_PORT, WIFI_RST_PIN);
  vTaskDelay(pdMS_TO_TICKS(1000));
}

/* ================================================================
 *  UART3 初始化 (参考野火 ESP8266_USART_Config)
 * ================================================================ */
static void wifi_uart_init(void)
{
  GPIO_InitTypeDef  gpio;
  USART_InitTypeDef usart;
  NVIC_InitTypeDef  nvic;

  RCC_AHB1PeriphClockCmd(WIFI_UART_TX_CLK | WIFI_UART_RX_CLK, ENABLE);
  RCC_APB1PeriphClockCmd(WIFI_UART_CLK, ENABLE);

  GPIO_PinAFConfig(WIFI_UART_TX_PORT, WIFI_UART_TX_SOURCE, WIFI_UART_TX_AF);
  GPIO_PinAFConfig(WIFI_UART_RX_PORT, WIFI_UART_RX_SOURCE, WIFI_UART_RX_AF);

  gpio.GPIO_Mode  = GPIO_Mode_AF;
  gpio.GPIO_OType = GPIO_OType_PP;
  gpio.GPIO_PuPd  = GPIO_PuPd_UP;
  gpio.GPIO_Speed = GPIO_Speed_50MHz;

  gpio.GPIO_Pin = WIFI_UART_TX_PIN;
  GPIO_Init(WIFI_UART_TX_PORT, &gpio);

  gpio.GPIO_Pin = WIFI_UART_RX_PIN;
  GPIO_Init(WIFI_UART_RX_PORT, &gpio);

  usart.USART_BaudRate            = WIFI_UART_BAUDRATE;
  usart.USART_WordLength          = USART_WordLength_8b;
  usart.USART_StopBits            = USART_StopBits_1;
  usart.USART_Parity              = USART_Parity_No;
  usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
  usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
  USART_Init(WIFI_UART, &usart);

  USART_ITConfig(WIFI_UART, USART_IT_RXNE, ENABLE);
  USART_ITConfig(WIFI_UART, USART_IT_IDLE, ENABLE);
  USART_Cmd(WIFI_UART, ENABLE);

  nvic.NVIC_IRQChannel                   = USART3_IRQn;
  nvic.NVIC_IRQChannelPreemptionPriority = 0x0F;
  nvic.NVIC_IRQChannelSubPriority        = 0x00;
  nvic.NVIC_IRQChannelCmd                = ENABLE;
  NVIC_Init(&nvic);
}

static void wifi_uart_send_byte(uint8_t byte)
{
  while (USART_GetFlagStatus(WIFI_UART, USART_FLAG_TXE) == RESET) {}
  USART_SendData(WIFI_UART, byte);
}

static void wifi_uart_send_str(const char *str)
{
  while (*str) {
    wifi_uart_send_byte((uint8_t)*str);
    str++;
  }
}

/* ================================================================
 *  AT 指令发送与响应检测 (参考野火 ESP8266_Cmd)
 * ================================================================ */
static bool ESP8266_Cmd(const char *cmd, const char *reply1,
                        const char *reply2, uint32_t waittime)
{
  wifi_rx_reset();

  wifi_uart_send_str(cmd);
  wifi_uart_send_byte('\r');
  wifi_uart_send_byte('\n');

  if (reply1 == NULL && reply2 == NULL) {
    return true;
  }

  vTaskDelay(pdMS_TO_TICKS(waittime));

  wifi_rx_buf[wifi_rx_len] = '\0';
  wifi_rx_len              = 0U;
  wifi_rx_finish_flag      = 0U;

  if (reply1 != NULL && reply2 != NULL) {
    return (strstr(wifi_rx_buf, reply1) != NULL) ||
           (strstr(wifi_rx_buf, reply2) != NULL);
  } else if (reply1 != NULL) {
    return (strstr(wifi_rx_buf, reply1) != NULL);
  } else {
    return (strstr(wifi_rx_buf, reply2) != NULL);
  }
}

/* ================================================================
 *  ESP8266 基本 AT 函数 (参考野火 bsp_esp8266.c)
 * ================================================================ */
static bool ESP8266_AT_Test(void)
{
  uint8_t cnt = 0U;

  GPIO_SetBits(WIFI_RST_PORT, WIFI_RST_PIN);
  printf("WiFi: AT test...\r\n");
  vTaskDelay(pdMS_TO_TICKS(2000));

  while (cnt < 10U) {
    printf("WiFi: AT attempt %u\r\n", (unsigned)cnt);
    if (ESP8266_Cmd("AT", "OK", NULL, 500)) {
      printf("WiFi: AT OK\r\n");
      return true;
    }
    wifi_hw_reset();
    cnt++;
  }
  return false;
}

static bool ESP8266_DHCP_CUR(void)
{
  return ESP8266_Cmd("AT+CWDHCP_CUR=1,1", "OK", NULL, 500);
}

static bool ESP8266_Net_Mode_Choose(uint8_t mode)
{
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+CWMODE=%u", (unsigned)mode);
  return ESP8266_Cmd(cmd, "OK", "no change", 2500);
}

static bool ESP8266_JoinAP(const char *ssid, const char *pwd)
{
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, pwd);
  return ESP8266_Cmd(cmd, "OK", NULL, 8000);
}

static bool ESP8266_Enable_MultipleId(uint8_t enable)
{
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+CIPMUX=%u", (unsigned)enable);
  return ESP8266_Cmd(cmd, "OK", NULL, 500);
}

static bool ESP8266_Link_Server(const char *ip, const char *port)
{
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%s", ip, port);
  return ESP8266_Cmd(cmd, "OK", "ALREAY CONNECT", 4000);
}

static bool ESP8266_UnvarnishSend(void)
{
  if (!ESP8266_Cmd("AT+CIPMODE=1", "OK", NULL, 500)) {
    return false;
  }
  return ESP8266_Cmd("AT+CIPSEND", "OK", ">", 500);
}

static void ESP8266_ExitUnvarnishSend(void)
{
  vTaskDelay(pdMS_TO_TICKS(1000));
  wifi_uart_send_str("+++");
  vTaskDelay(pdMS_TO_TICKS(500));
}

static uint8_t ESP8266_Get_LinkStatus(void)
{
  if (ESP8266_Cmd("AT+CIPSTATUS", "OK", NULL, 500)) {
    if (strstr(wifi_rx_buf, "STATUS:2\r\n")) return 2U;
    if (strstr(wifi_rx_buf, "STATUS:3\r\n")) return 3U;
    if (strstr(wifi_rx_buf, "STATUS:4\r\n")) return 4U;
  }
  return 0U;
}

/* ================================================================
 *  WiFi 配置初始化序列 (参考野火 ESP8266_StaTcpClient_Unvarnish_ConfigTest)
 * ================================================================ */
static int wifi_config_sequence(void)
{
  printf("WiFi: resetting ESP8266...\r\n");
  wifi_hw_reset();

  GPIO_SetBits(WIFI_EN_PORT, WIFI_EN_PIN);

  if (!ESP8266_AT_Test()) {
    printf("WiFi error: AT test failed\r\n");
    return -1;
  }

  if (!ESP8266_DHCP_CUR()) {
    printf("WiFi error: DHCP config failed\r\n");
    return -1;
  }

  printf("WiFi: setting STA mode...\r\n");
  if (!ESP8266_Net_Mode_Choose(1U /* STA */)) {
    printf("WiFi error: STA mode failed\r\n");
    return -1;
  }

  printf("WiFi: connecting AP \"%s\"...\r\n", WIFI_SSID);
  if (!ESP8266_JoinAP(WIFI_SSID, WIFI_PASSWORD)) {
    printf("WiFi error: join AP failed\r\n");
    return -1;
  }
  printf("WiFi: AP connected\r\n");

  printf("WiFi: disable multi-connection...\r\n");
  if (!ESP8266_Enable_MultipleId(0U /* DISABLE */)) {
    printf("WiFi error: set single connection failed\r\n");
    return -1;
  }

  {
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", WIFI_OTA_SERVER_PORT);
    printf("WiFi: connecting TCP %s:%s ...\r\n", WIFI_OTA_SERVER_IP, port_str);
    if (!ESP8266_Link_Server(WIFI_OTA_SERVER_IP, port_str)) {
      printf("WiFi error: TCP connect failed\r\n");
      return -1;
    }
  }
  printf("WiFi: TCP connected\r\n");

  printf("WiFi: entering transparent mode...\r\n");
  if (!ESP8266_UnvarnishSend()) {
    printf("WiFi error: enter transparent mode failed\r\n");
    return -1;
  }
  printf("WiFi: transparent mode ready\r\n");

  return 0;
}

/* ================================================================
 *  CRC16-Modbus
 * ================================================================ */
static uint16_t wifi_crc16(const uint8_t *data, uint32_t len)
{
  uint16_t crc = 0xFFFFU;
  uint32_t i;
  int bit;

  for (i = 0U; i < len; i++) {
    crc ^= data[i];
    for (bit = 0; bit < 8; bit++) {
      if ((crc & 1U) != 0U) {
        crc = (uint16_t)((crc >> 1) ^ 0xA001U);
      } else {
        crc = (uint16_t)(crc >> 1);
      }
    }
  }
  return crc;
}

/* ================================================================
 *  Flash 写入函数
 * ================================================================ */
static FLASH_Status wifi_flash_program_word(uint32_t addr, uint32_t data)
{
  FLASH_Status st;

  (void)FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                        FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

  st = FLASH_ProgramWord(addr, data);
  if (st != FLASH_COMPLETE) {
    (void)FLASH_WaitForLastOperation();
    return st;
  }
  return FLASH_WaitForLastOperation();
}

static FLASH_Status wifi_flash_program_byte(uint32_t addr, uint8_t data)
{
  FLASH_Status st;

  (void)FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                        FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

  st = FLASH_ProgramByte(addr, data);
  if (st != FLASH_COMPLETE) {
    (void)FLASH_WaitForLastOperation();
    return st;
  }
  return FLASH_WaitForLastOperation();
}

static FLASH_Status wifi_flash_write_bytes(uint32_t addr, const uint8_t *data, uint16_t len)
{
  uint16_t idx = 0U;

  if (data == NULL || len == 0U) return FLASH_COMPLETE;

  while (idx < len) {
    FLASH_Status st;

    if (((addr & 0x3U) == 0U) && ((uint16_t)(idx + 4U) <= len)) {
      uint32_t w = ((uint32_t)data[idx + 0U]) |
                   ((uint32_t)data[idx + 1U] << 8) |
                   ((uint32_t)data[idx + 2U] << 16) |
                   ((uint32_t)data[idx + 3U] << 24);
      st = wifi_flash_program_word(addr, w);
      if (st != FLASH_COMPLETE) return st;
      addr += 4U;
      idx  += 4U;
    } else {
      st = wifi_flash_program_byte(addr, data[idx]);
      if (st != FLASH_COMPLETE) return st;
      addr += 1U;
      idx  += 1U;
    }
  }
  return FLASH_COMPLETE;
}

static FLASH_Status wifi_flash_erase_slot(uint8_t boot_from)
{
  FLASH_Status st;

  if (boot_from == 0U) {
    st = FLASH_EraseSector(FLASH_Sector_6, VoltageRange_3);
    if (st != FLASH_COMPLETE) return st;
    st = FLASH_EraseSector(FLASH_Sector_7, VoltageRange_3);
    return st;
  }
  st = FLASH_EraseSector(FLASH_Sector_8, VoltageRange_3);
  if (st != FLASH_COMPLETE) return st;
  st = FLASH_EraseSector(FLASH_Sector_9, VoltageRange_3);
  return st;
}

static FLASH_Status wifi_flash_write_flag(uint8_t boot_from, uint32_t image_size)
{
  FLASH_Status st;
  uint32_t flag_words[4];
  uint32_t addr;
  uint32_t i;

  flag_words[0] = 0x424F4F54U;
  flag_words[1] = 0x00000001U;
  flag_words[2] = boot_from;
  flag_words[3] = image_size;

  st = FLASH_EraseSector(FLASH_Sector_3, VoltageRange_3);
  if (st != FLASH_COMPLETE) return st;

  addr = IAP_FLAG_SECTOR_START;
  for (i = 0U; i < 4U; i++) {
    st = wifi_flash_program_word(addr, flag_words[i]);
    if (st != FLASH_COMPLETE) return st;
    addr += 4U;
  }
  return FLASH_COMPLETE;
}

static uint8_t wifi_get_target_boot_from(void)
{
  uint32_t magic = *(volatile uint32_t *)IAP_FLAG_SECTOR_START;
  if (magic != 0x424F4F54U) return 1U;
  return (*(volatile uint32_t *)(IAP_FLAG_SECTOR_START + 8U) == 0U) ? 1U : 0U;
}

/* ================================================================
 *  IAP 帧协议解析
 *
 *  帧: 0x5A 0xA5 | CMD(2B) | N(2B,BE) | Payload(N) | Reserved(4B,0) | CRC16(2B,LE)
 * ================================================================ */
#define WIFI_OTA_HDR0          0x5AU
#define WIFI_OTA_HDR1          0xA5U
#define WIFI_OTA_CMD_FW_H      0x00U
#define WIFI_OTA_CMD_FW_L      0x01U
#define WIFI_OTA_CMD_IAP_H     0x00U
#define WIFI_OTA_CMD_IAP_L     0x02U
#define WIFI_OTA_FRAME_MAX_N   256U
#define WIFI_OTA_FRAME_OVERHEAD 12U

typedef struct {
  uint8_t  active;
  uint8_t  target_boot_from;
  uint32_t slot_start;
  uint32_t write_addr;
  uint32_t bytes_written;
  uint32_t last_rx_ms;
} WifiOtaSession_t;

static uint32_t wifi_ota_now_ms(void)
{
  return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void wifi_ota_session_reset(WifiOtaSession_t *s)
{
  memset(s, 0, sizeof(*s));
}

static int wifi_ota_begin(WifiOtaSession_t *s)
{
  FLASH_Status st;

  wifi_ota_session_reset(s);
  s->target_boot_from = wifi_get_target_boot_from();
  s->slot_start  = (s->target_boot_from == 0U) ? IAP_SLOT_A_START : IAP_SLOT_B_START;
  s->write_addr  = s->slot_start;
  s->last_rx_ms  = wifi_ota_now_ms();

  printf("WiFi OTA: prepare slot %c at 0x%08lX\r\n",
         (s->target_boot_from == 0U) ? 'A' : 'B',
         (unsigned long)s->slot_start);

  FLASH_Unlock();
  st = wifi_flash_erase_slot(s->target_boot_from);
  FLASH_Lock();

  if (st != FLASH_COMPLETE) {
    printf("WiFi OTA error: erase slot failed st=%d\r\n", (int)st);
    wifi_ota_session_reset(s);
    return -1;
  }

  s->active = 1U;
  return 0;
}

static int wifi_ota_write_chunk(WifiOtaSession_t *s, const uint8_t *data, uint16_t len)
{
  FLASH_Status st;

  if (s->active == 0U) {
    if (wifi_ota_begin(s) != 0) return -1;
  }

  if ((s->bytes_written + (uint32_t)len) > IAP_RUN_SIZE) {
    printf("WiFi OTA error: image too large (%lu)\r\n",
           (unsigned long)(s->bytes_written + (uint32_t)len));
    return -1;
  }

  FLASH_Unlock();
  st = wifi_flash_write_bytes(s->write_addr, data, len);
  FLASH_Lock();

  if (st != FLASH_COMPLETE) {
    printf("WiFi OTA error: flash write at 0x%08lX st=%d\r\n",
           (unsigned long)s->write_addr, (int)st);
    return -1;
  }

  s->write_addr    += len;
  s->bytes_written += len;
  s->last_rx_ms     = wifi_ota_now_ms();

  printf("WiFi OTA: slot %c progress %lu bytes\r\n",
         (s->target_boot_from == 0U) ? 'A' : 'B',
         (unsigned long)s->bytes_written);
  return 0;
}

static void wifi_ota_commit_and_reboot(WifiOtaSession_t *s)
{
  FLASH_Status st;

  if (s->active == 0U || s->bytes_written == 0U) return;

  printf("WiFi OTA: finalize slot %c, size=%lu\r\n",
         (s->target_boot_from == 0U) ? 'A' : 'B',
         (unsigned long)s->bytes_written);

  FLASH_Unlock();
  st = wifi_flash_write_flag(s->target_boot_from, s->bytes_written);
  FLASH_Lock();

  if (st != FLASH_COMPLETE) {
    printf("WiFi OTA error: write flag failed st=%d\r\n", (int)st);
    wifi_ota_session_reset(s);
    return;
  }

  printf("WiFi OTA: flag updated, rebooting...\r\n");
  vTaskDelay(pdMS_TO_TICKS(100));
  NVIC_SystemReset();
}

/*
 * 将 IDLE 帧缓冲区的数据送入 IAP 帧解析器。
 * 透传模式下，TCP 固件数据流经 ESP8266 UART3 到达，
 * IDLE 中断在数据包间隙将数据分出帧块。
 */
static void wifi_ota_parse_frame_data(WifiOtaSession_t *s,
                                      const uint8_t *data, uint16_t data_len)
{
  static uint8_t  stream_buf[WIFI_RX_BUF_MAX_LEN];
  static uint16_t stream_len;
  uint16_t i;

  /* 追加到累积缓冲区 */
  for (i = 0U; i < data_len; i++) {
    if (stream_len < (uint16_t)(sizeof(stream_buf) - 1U)) {
      stream_buf[stream_len] = data[i];
      stream_len++;
    } else {
      memmove(stream_buf, stream_buf + (stream_len / 2U), stream_len / 2U);
      stream_len /= 2U;
      stream_buf[stream_len] = data[i];
      stream_len++;
    }
  }

  /* 帧解析 */
  while (stream_len >= 2U) {
    uint16_t payload_len;
    uint16_t frame_len;
    uint16_t crc_offset;
    uint16_t crc_rx;
    uint16_t crc_calc;
    uint8_t  cmd_h;
    uint8_t  cmd_l;
    uint32_t k;
    uint8_t  skip;

    if (stream_buf[0] != WIFI_OTA_HDR0 || stream_buf[1] != WIFI_OTA_HDR1) {
      memmove(stream_buf, stream_buf + 1, (size_t)(stream_len - 1U));
      stream_len--;
      continue;
    }

    if (stream_len < 6U) return;

    cmd_h = stream_buf[2];
    cmd_l = stream_buf[3];

    payload_len = (uint16_t)(((uint16_t)stream_buf[4] << 8) | stream_buf[5]);
    if (payload_len > WIFI_OTA_FRAME_MAX_N) {
      memmove(stream_buf, stream_buf + 2, (size_t)(stream_len - 2U));
      stream_len -= 2U;
      continue;
    }

    frame_len = (uint16_t)(payload_len + WIFI_OTA_FRAME_OVERHEAD);
    if (stream_len < frame_len) return;

    skip = 0U;
    for (k = 0U; k < 4U; k++) {
      if (stream_buf[6U + payload_len + k] != 0U) { skip = 1U; break; }
    }
    if (skip) {
      memmove(stream_buf, stream_buf + 1, (size_t)(stream_len - 1U));
      stream_len--;
      continue;
    }

    crc_offset = (uint16_t)(10U + payload_len);
    crc_calc   = wifi_crc16(stream_buf, crc_offset);
    crc_rx     = (uint16_t)stream_buf[crc_offset] |
                 (uint16_t)((uint16_t)stream_buf[crc_offset + 1U] << 8);

    if (crc_calc != crc_rx) {
      memmove(stream_buf, stream_buf + 2, (size_t)(stream_len - 2U));
      stream_len -= 2U;
      continue;
    }

    if (cmd_h == WIFI_OTA_CMD_IAP_H && cmd_l == WIFI_OTA_CMD_IAP_L) {
      printf("WiFi OTA: ENTER_IAP received\r\n");
      if (wifi_ota_begin(s) != 0) wifi_ota_session_reset(s);
    } else if (cmd_h == WIFI_OTA_CMD_FW_H && cmd_l == WIFI_OTA_CMD_FW_L) {
      if (wifi_ota_write_chunk(s, &stream_buf[6], payload_len) != 0) {
        wifi_ota_session_reset(s);
      }
    }

    memmove(stream_buf, stream_buf + frame_len, (size_t)(stream_len - frame_len));
    stream_len = (uint16_t)(stream_len - frame_len);
  }
}

/* ================================================================
 *  TCP 断开后重连序列
 * ================================================================ */
static int wifi_reconnect_tcp(void)
{
  uint8_t status;

  ESP8266_ExitUnvarnishSend();

  do {
    status = ESP8266_Get_LinkStatus();
  } while (status == 0U);

  if (status == 4U /* disconnected */) {
    char port_str[8];

    printf("WiFi: reconnecting...\r\n");

    if (!ESP8266_JoinAP(WIFI_SSID, WIFI_PASSWORD)) {
      printf("WiFi error: rejoin AP failed\r\n");
      return -1;
    }

    snprintf(port_str, sizeof(port_str), "%d", WIFI_OTA_SERVER_PORT);
    if (!ESP8266_Link_Server(WIFI_OTA_SERVER_IP, port_str)) {
      printf("WiFi error: reconnect TCP failed\r\n");
      return -1;
    }

    printf("WiFi: reconnected\r\n");
  }

  if (!ESP8266_UnvarnishSend()) {
    printf("WiFi error: re-enter transparent mode failed\r\n");
    return -1;
  }

  return 0;
}

/* ================================================================
 *  WiFi OTA 主任务
 *
 *  流程:
 *    1. 初始化 UART3 + ESP8266 引脚
 *    2. AT 配置序列：STA → DHCP → Join AP → TCP Connect → 透传
 *    3. 透传接收 IAP 帧 → 写入 Flash
 *    4. TCP 断开时自动重连
 *    5. 空闲超时后提交标志并复位
 * ================================================================ */
void IAP_WIFI_Task(void *parameter)
{
  WifiOtaSession_t session;
  uint32_t conn_retry_ms;

  (void)parameter;
  wifi_ota_session_reset(&session);
  conn_retry_ms = 0U;

  printf("WiFi OTA: task started\r\n");

  for (;;) {
    if (conn_retry_ms > 0U) {
      printf("WiFi: retry in %lu ms...\r\n", (unsigned long)conn_retry_ms);
      vTaskDelay(pdMS_TO_TICKS(conn_retry_ms));
    }

    if (wifi_config_sequence() != 0) {
      conn_retry_ms = 5000U;
      continue;
    }

    conn_retry_ms = 5000U;
    wifi_ota_session_reset(&session);
    wifi_rx_reset();

    printf("WiFi OTA: waiting for firmware data...\r\n");

    for (;;) {
      /* 检查 TCP 断开 */
      if (wifi_tcp_closed_flag != 0U) {
        printf("WiFi: TCP closed by remote\r\n");
        wifi_rx_reset();
        wifi_ota_session_reset(&session);
        if (wifi_reconnect_tcp() != 0) {
          break;
        }
        continue;
      }

      /* 等待 IDLE 帧完成 */
      if (wifi_rx_finish_flag != 0U) {
        uint16_t len;

        __disable_irq();
        len = wifi_rx_len;
        wifi_rx_finish_flag = 0U;
        __enable_irq();

        if (len > 0U) {
          /* 将原始字节送入 IAP 帧解析器 */
          wifi_ota_parse_frame_data(&session, (const uint8_t *)wifi_rx_buf, len);

          if (session.active != 0U) {
            session.last_rx_ms = wifi_ota_now_ms();
          }
        }

        /* 重置接收缓冲区准备下一帧 */
        __disable_irq();
        wifi_rx_len = 0U;
        __enable_irq();
      }

      /* 空闲超时检查 */
      if (session.active != 0U && session.bytes_written > 0U) {
        uint32_t idle_ms = wifi_ota_now_ms() - session.last_rx_ms;
        if (idle_ms >= WIFI_OTA_IDLE_TIMEOUT_MS) {
          printf("WiFi OTA: idle %lu ms, finalizing...\r\n", (unsigned long)idle_ms);
          wifi_ota_commit_and_reboot(&session);
          break;
        }
      }

      vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
}

/* ================================================================
 *  Public Init
 * ================================================================ */
void IAP_WIFI_Init(void)
{
  wifi_hw_gpio_init();
  wifi_uart_init();
  printf("WiFi: UART3 + ESP8266 initialized (IDLE interrupt mode)\r\n");
}
