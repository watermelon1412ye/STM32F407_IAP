#ifndef IAP_WIFI_H
#define IAP_WIFI_H

#include <stdint.h>

/* ================================================================
 *  WiFi 模组硬件引脚定义 (ESP8266EX via UART3)
 *
 *  霸天虎 F407-V2 原理图:
 *    WIFI_TXD (PB11)  — USART3_RX
 *    WIFI_RXD (PB10)  — USART3_TX
 *    WIFI_RST (PG15)  — ESP8266 EXT_RSTB
 *    WIFI_EN  (PE2)   — ESP8266 CHIP_EN
 * ================================================================ */
#define WIFI_UART               USART3
#define WIFI_UART_CLK           RCC_APB1Periph_USART3
#define WIFI_UART_BAUDRATE      115200

#define WIFI_UART_TX_PORT       GPIOB
#define WIFI_UART_TX_CLK        RCC_AHB1Periph_GPIOB
#define WIFI_UART_TX_PIN        GPIO_Pin_10
#define WIFI_UART_TX_AF         GPIO_AF_USART3
#define WIFI_UART_TX_SOURCE     GPIO_PinSource10

#define WIFI_UART_RX_PORT       GPIOB
#define WIFI_UART_RX_CLK        RCC_AHB1Periph_GPIOB
#define WIFI_UART_RX_PIN        GPIO_Pin_11
#define WIFI_UART_RX_AF         GPIO_AF_USART3
#define WIFI_UART_RX_SOURCE     GPIO_PinSource11

#define WIFI_RST_PORT           GPIOG
#define WIFI_RST_CLK            RCC_AHB1Periph_GPIOG
#define WIFI_RST_PIN            GPIO_Pin_15

#define WIFI_EN_PORT            GPIOE
#define WIFI_EN_CLK             RCC_AHB1Periph_GPIOE
#define WIFI_EN_PIN             GPIO_Pin_2

/* ================================================================
 *  WiFi 配置参数 (按实际环境修改)
 * ================================================================ */
#define WIFI_SSID               "RIFA" //你的WiFi名
#define WIFI_PASSWORD           "RIFA2022" //你的WiFi密码
#define WIFI_OTA_SERVER_IP      "192.168.1.200"
#define WIFI_OTA_SERVER_PORT    6000

/* ================================================================
 *  内部参数
 * ================================================================ */
#define WIFI_RX_BUF_SIZE        2048U
#define WIFI_AT_RESP_TIMEOUT_MS 5000U
#define WIFI_AT_RETRY_MAX       3U
#define WIFI_OTA_IDLE_TIMEOUT_MS 1500U

#define WIFI_TASK_STACK_WORDS   512

/* ================================================================
 *  Public API
 * ================================================================ */
void IAP_WIFI_Init(void);
void IAP_WIFI_Task(void *parameter);

/* ISR 回调 */
void IAP_WIFI_IrqRxByte(uint8_t byte);
void IAP_WIFI_IrqOnIdle(void);

#endif
