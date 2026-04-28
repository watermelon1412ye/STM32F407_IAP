#ifndef IAP_UART_H
#define IAP_UART_H

#include <stdint.h>
#include "stm32f4xx_flash.h"
#include "iap_layout.h"

#define IAP_UART_RX_BUFFER_SIZE   1024U
#define IAP_FRAME_MAX_N           256U
#define IAP_FRAME_OVERHEAD        12U
#define IAP_IDLE_TIMEOUT_MS       25U
#define IAP_JUMP_IDLE_TIMEOUT_MS 1500U

#define IAP_CMD_FIRMWARE_H        0x00U
#define IAP_CMD_FIRMWARE_L        0x01U
#define IAP_CMD_ENTER_IAP_H       0x00U
#define IAP_CMD_ENTER_IAP_L       0x02U

extern uint8_t rx_buffer[IAP_UART_RX_BUFFER_SIZE];
extern uint32_t flash_write_addr;
extern volatile uint8_t g_iap_enter_requested;

void IAP_UART_Init(void);
void IAP_UART_IrqRxByte(uint8_t byte);
void IAP_UART_OnRxOverflow(void);
void IAP_UART_OnSysTick1ms(void);

uint8_t IAP_UART_FrameReady(void);
uint32_t IAP_UART_GetIdleMs(void);
void Parse_IAP_Frame(void);
uint16_t CRC16_Modbus(const uint8_t *data, uint32_t len);
void IAP_Load_App(uint32_t app_addr);
void IAP_UART_ResetSession(void);
void IAP_UART_SetFlashProgramAllowed(uint8_t allow);
FLASH_Status IAP_UART_PrepareDownloadSlot(void);
uint8_t IAP_UART_HasReceivedFirmware(void);
FLASH_Status IAP_UART_FinalizeDownload(void);

#endif
