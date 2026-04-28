/**
  ******************************************************************************
  * @file    bsp_debug_usart.c
  * @author  fire
  * @version V1.0
  * @date    2015-xx-xx
  * @brief   ?????c??printf?????????usart???????????��???? ??????????��?
  ******************************************************************************
  * @attention
  *
  * ?????:????  STM32 F407 ??????  
  * ???    :http://www.firebbs.cn
  * ???    :https://fire-stm32.taobao.com
  *
  ******************************************************************************
  */

#include "bsp_debug_usart.h"
#include "../iap/iap_uart.h"

void Debug_USART_RX_IRQ_Config(void)
{
  NVIC_InitTypeDef NVIC_InitStructure;

  NVIC_InitStructure.NVIC_IRQChannel = DEBUG_USART_IRQ;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);

  USART_ITConfig(DEBUG_USART, USART_IT_RXNE, ENABLE);
}

void Debug_USART_Config(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;
  USART_InitTypeDef USART_InitStructure;

  RCC_AHB1PeriphClockCmd(DEBUG_USART_RX_GPIO_CLK | DEBUG_USART_TX_GPIO_CLK, ENABLE);
  RCC_APB2PeriphClockCmd(DEBUG_USART_CLK, ENABLE);

  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_Pin = DEBUG_USART_TX_PIN;
  GPIO_Init(DEBUG_USART_TX_GPIO_PORT, &GPIO_InitStructure);

  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_Pin = DEBUG_USART_RX_PIN;
  GPIO_Init(DEBUG_USART_RX_GPIO_PORT, &GPIO_InitStructure);

  GPIO_PinAFConfig(DEBUG_USART_RX_GPIO_PORT, DEBUG_USART_RX_SOURCE, DEBUG_USART_RX_AF);
  GPIO_PinAFConfig(DEBUG_USART_TX_GPIO_PORT, DEBUG_USART_TX_SOURCE, DEBUG_USART_TX_AF);

  USART_InitStructure.USART_BaudRate = DEBUG_USART_BAUDRATE;
  USART_InitStructure.USART_WordLength = USART_WordLength_8b;
  USART_InitStructure.USART_StopBits = USART_StopBits_1;
  USART_InitStructure.USART_Parity = USART_Parity_No;
  USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
  USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
  USART_Init(DEBUG_USART, &USART_InitStructure);

  Debug_USART_RX_IRQ_Config();
  USART_Cmd(DEBUG_USART, ENABLE);
}

void USART1_IRQHandler(void)
{
  uint8_t c;

  if (USART_GetITStatus(DEBUG_USART, USART_IT_RXNE) == RESET)
  {
    return;
  }

  if (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_ORE) != RESET)
  {
    (void)DEBUG_USART->SR;
    (void)DEBUG_USART->DR;
    IAP_UART_OnRxOverflow();
    return;
  }

  c = (uint8_t)USART_ReceiveData(DEBUG_USART);
  IAP_UART_IrqRxByte(c);
}

void Usart_SendByte(USART_TypeDef *pUSARTx, uint8_t ch)
{
	/* ???????????USART */
	USART_SendData(pUSARTx,ch);
		
	/* ????????????? */
	while (USART_GetFlagStatus(pUSARTx, USART_FLAG_TXE) == RESET);	
}

/*****************  ???????????? **********************/
void Usart_SendString( USART_TypeDef * pUSARTx, char *str)
{
	unsigned int k=0;
  do 
  {
    Usart_SendByte(pUSARTx, *(str + k));
    k++;
  } while (*(str + k) != '\0');

  while (USART_GetFlagStatus(pUSARTx, USART_FLAG_TC) == RESET)
    ;
}

void Usart_SendHalfWord(USART_TypeDef *pUSARTx, uint16_t ch)
{
  uint8_t temp_h, temp_l;

  temp_h = (ch & 0XFF00) >> 8;
  temp_l = ch & 0XFF;

  USART_SendData(pUSARTx, temp_h);
  while (USART_GetFlagStatus(pUSARTx, USART_FLAG_TXE) == RESET)
    ;

  USART_SendData(pUSARTx, temp_l);
  while (USART_GetFlagStatus(pUSARTx, USART_FLAG_TXE) == RESET)
    ;
}

int fputc(int ch, FILE *f)
{
  USART_SendData(DEBUG_USART, (uint8_t)ch);
  while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_TXE) == RESET)
    ;
  return (ch);
}

int fgetc(FILE *f)
{
  while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_RXNE) == RESET)
    ;
  return (int)USART_ReceiveData(DEBUG_USART);
}
