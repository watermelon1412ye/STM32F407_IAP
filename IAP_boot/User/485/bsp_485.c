/**
  ******************************************************************************
  * @file    bsp_485.c
  * @author  fire
  * @version V1.0
  * @date    2015-xx-xx
  * @brief   485驱动
  ******************************************************************************
  * @attention
  *
  * 实验平台:野火  STM32 F407 开发板  
  * 论坛    :http://www.firebbs.cn
  * 淘宝    :https://fire-stm32.taobao.com
  *
  ******************************************************************************
  */ 
#include "./485/bsp_485.h"
#include <stdarg.h>


static void Delay(__IO u32 nCount); 


/// 配置USART接收中断
static void NVIC_Configuration(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;
    /* Configure the NVIC Preemption Priority Bits */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_0);

    /* Enable the USARTy Interrupt */
    NVIC_InitStructure.NVIC_IRQChannel = _485_INT_IRQ; 
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority =1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}
/*
 * 函数名：_485_Config
 * 描述  ：USART GPIO 配置,工作模式配置
 * 输入  ：无
 * 输出  : 无
 * 调用  ：外部调用
 */
void _485_Config(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;

	/* config USART clock */
	RCC_AHB1PeriphClockCmd(_485_USART_RX_GPIO_CLK|_485_USART_TX_GPIO_CLK|_485_RE_GPIO_CLK, ENABLE);
	RCC_APB1PeriphClockCmd(_485_USART_CLK, ENABLE); 

	
	  /* Connect PXx to USARTx_Tx*/
  GPIO_PinAFConfig(_485_USART_RX_GPIO_PORT,_485_USART_RX_SOURCE, _485_USART_RX_AF);

  /* Connect PXx to USARTx_Rx*/
  GPIO_PinAFConfig(_485_USART_TX_GPIO_PORT,_485_USART_TX_SOURCE,_485_USART_TX_AF);

	
	/* USART GPIO config */
   /* Configure USART Tx as alternate function push-pull */
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;

  GPIO_InitStructure.GPIO_Pin = _485_USART_TX_PIN  ;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(_485_USART_TX_GPIO_PORT, &GPIO_InitStructure);

  /* Configure USART Rx as alternate function  */
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF; 
	GPIO_InitStructure.GPIO_Pin = _485_USART_RX_PIN;
  GPIO_Init(_485_USART_RX_GPIO_PORT, &GPIO_InitStructure);

  
  /* 485收发控制管脚 */
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
  GPIO_InitStructure.GPIO_Pin = _485_RE_PIN  ;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(_485_RE_GPIO_PORT, &GPIO_InitStructure);
	  
	/* USART mode config */
	USART_InitStructure.USART_BaudRate = _485_USART_BAUDRATE;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No ;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

	USART_Init(_485_USART, &USART_InitStructure); 
  USART_Cmd(_485_USART, ENABLE);
	
	NVIC_Configuration();
	/* 使能串口接收中断 */
	USART_ITConfig(_485_USART, USART_IT_RXNE, ENABLE);
	
	_485_RX_EN(); //默认进入接收模式（随极性宏自动匹配）
}



/* 仅等待 TXE：数据已进入移位寄存器，可写下一字节（用于连续发送流水线） */
static void _485_WaitTxe(void)
{
	while (USART_GetFlagStatus(_485_USART, USART_FLAG_TXE) == RESET);
}

/* 等待移位寄存器空（整字节含停止位已发出） */
static void _485_WaitTc(void)
{
	while (USART_GetFlagStatus(_485_USART, USART_FLAG_TC) == RESET);
}

/***************** 发送一个字符（调用方需自行 _485_TX_EN / _485_RX_EN） **********************/
void _485_SendByte(uint8_t ch)
{
	USART_SendData(_485_USART, ch);
	_485_WaitTxe();
	_485_WaitTc();
	USART_ClearFlag(_485_USART, USART_FLAG_TC);
}
/*****************  发送指定长度的字符串 **********************/
void _485_SendStr_length( uint8_t *str,uint32_t strlen )
{
	unsigned int k=0;

	_485_TX_EN()	;//	使能发送数据
	for (k = 0; k < strlen; k++)
	{
		USART_SendData(_485_USART, *(str + k));
		_485_WaitTxe();
	}
	/* Modbus/485：必须在停止位完全发出后再拉低 DE，否则末字节与 CRC 会被截断，主站 CRC 必错 */
	_485_WaitTc();
	USART_ClearFlag(_485_USART, USART_FLAG_TC);

	Delay(0xFFF);

	_485_RX_EN()	;//	使能接收数据
}


/*****************  发送字符串 **********************/
void _485_SendString(  uint8_t *str)
{
	unsigned int k=0;
	
	_485_TX_EN()	;//	使能发送数据
	
	while (*(str + k) != '\0')
	{
		USART_SendData(_485_USART, *(str + k));
		_485_WaitTxe();
		k++;
	}

	_485_WaitTc();
	USART_ClearFlag(_485_USART, USART_FLAG_TC);

	Delay(0xFFF);
		
	_485_RX_EN()	;//	使能接收数据
}









// 中断缓存串口数据
#define UART_BUFF_SIZE      1024
volatile    uint16_t uart_p = 0;
uint8_t     uart_buff[UART_BUFF_SIZE];

// 【必须改成这个名字，STM32的硬件才能认出来！】
void USART2_IRQHandler(void) 
{
    if(uart_p<UART_BUFF_SIZE)
    {
        if(USART_GetITStatus(_485_USART, USART_IT_RXNE) != RESET)
        {
            uart_buff[uart_p] = USART_ReceiveData(_485_USART);
            uart_p++;
            USART_ClearITPendingBit(_485_USART, USART_IT_RXNE);
        }
    }
    else
    {
        USART_ClearITPendingBit(_485_USART, USART_IT_RXNE);
    }
}



//获取接收到的数据和长度
char *get_rebuff(uint16_t *len) 
{
    *len = uart_p;
    return (char *)&uart_buff;
}

// 修改 bsp_485.c 中的清空缓存区函数
void clean_rebuff(void) 
{
    uint16_t i;
    for(i = 0; i < UART_BUFF_SIZE; i++) {
        uart_buff[i] = 0;
    }
    uart_p = 0;
}




static void Delay(__IO uint32_t nCount)	 //简单的延时函数
{
	for(; nCount != 0; nCount--);
}