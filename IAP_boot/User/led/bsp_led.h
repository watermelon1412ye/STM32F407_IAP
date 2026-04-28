#ifndef __LED_H
#define	__LED_H

#include "stm32f4xx.h"

//引脚定义
/*******************************************************/
//R 红色LED
#define LED1_PIN                  GPIO_Pin_6                 
#define LED1_GPIO_PORT            GPIOF                      
#define LED1_GPIO_CLK             RCC_AHB1Periph_GPIOF

//G 绿色LED
#define LED2_PIN                  GPIO_Pin_7                 
#define LED2_GPIO_PORT            GPIOF                      
#define LED2_GPIO_CLK             RCC_AHB1Periph_GPIOF

//B 蓝色LED
#define LED3_PIN                  GPIO_Pin_8                 
#define LED3_GPIO_PORT            GPIOF                       
#define LED3_GPIO_CLK             RCC_AHB1Periph_GPIOF
/************************************************************/


/** 定义LED的宏，用于控制LED的亮灭
	* LED亮灭状态，ON=0，OFF=1
	* LED亮灭状态，ON=1，OFF=0
	*/
#define ON  0
#define OFF 1

/* 带参宏，可以像内联函数一样使用 */
#define LED1(a)	if (a)	\
					GPIO_SetBits(LED1_GPIO_PORT,LED1_PIN);\
					else		\
					GPIO_ResetBits(LED1_GPIO_PORT,LED1_PIN)

#define LED2(a)	if (a)	\
					GPIO_SetBits(LED2_GPIO_PORT,LED2_PIN);\
					else		\
					GPIO_ResetBits(LED2_GPIO_PORT,LED2_PIN)

#define LED3(a)	if (a)	\
					GPIO_SetBits(LED3_GPIO_PORT,LED3_PIN);\
					else		\
					GPIO_ResetBits(LED3_GPIO_PORT,LED3_PIN)


/* 直接操作寄存器的方法控制IO */
#define	digitalHi(p,i)			 {p->BSRRL=i;}		//设置为高电平
#define digitalLo(p,i)			 {p->BSRRH=i;}		//输出低电平
#define digitalToggle(p,i)	 {p->ODR ^=i;}		//输出反转状态

/* 直接操作寄存器的方法控制IO */
#define LED1_TOGGLE		digitalToggle(LED1_GPIO_PORT,LED1_PIN)
#define LED1_OFF			digitalHi(LED1_GPIO_PORT,LED1_PIN)
#define LED1_ON				digitalLo(LED1_GPIO_PORT,LED1_PIN)

/*
 * 定义LED的宏，用于控制LED的亮灭
 * 红色LED的亮灭状态，LED_RED_TOGGLE匹配ARM Compiler 5的LED_RED
 */
#define LED2_TOGGLE		digitalToggle(LED2_GPIO_PORT,LED2_PIN)
#define LED2_OFF			digitalHi(LED2_GPIO_PORT,LED2_PIN)
#define LED2_ON				digitalLo(LED2_GPIO_PORT,LED2_PIN)

#define LED3_TOGGLE		digitalToggle(LED3_GPIO_PORT,LED3_PIN)
#define LED3_OFF			digitalHi(LED3_GPIO_PORT,LED3_PIN)
#define LED3_ON				digitalLo(LED3_GPIO_PORT,LED3_PIN)

/* 定义LED的宏，用于控制LED的亮灭 */

//红色LED
#define LED_RED  \
					LED1_ON;\
					LED2_OFF;\
					LED3_OFF

//绿色LED
#define LED_GREEN		\
					LED1_OFF;\
					LED2_ON;\
					LED3_OFF

//蓝色LED
#define LED_BLUE	\
					LED1_OFF;\
					LED2_OFF;\
					LED3_ON

					
//黄色LED(红色+绿色)					
#define LED_YELLOW	\
					LED1_ON;\
					LED2_ON;\
					LED3_OFF
//紫色LED(蓝色+绿色)
#define LED_PURPLE	\
					LED1_ON;\
					LED2_OFF;\
					LED3_ON

//青色LED(蓝色+红色)
#define LED_CYAN \
					LED1_OFF;\
					LED2_ON;\
					LED3_ON
					
//白色LED(红色+绿色+蓝色)
#define LED_WHITE	\
					LED1_ON;\
					LED2_ON;\
					LED3_ON
					
//所有LED关闭
#define LED_RGBOFF	\
					LED1_OFF;\
					LED2_OFF;\
					LED3_OFF		




void LED_GPIO_Config(void);

/* 配置LED1的GPIO，用于控制LED1的亮灭 */
void BSP_RedLed_Toggle(void);
void BSP_RedLed_On(void);
void BSP_RedLed_Off(void);

#endif /* __LED_H */
