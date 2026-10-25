#include "stm32f10x.h"                  // Device header
#include "PWM.h"
#include "PetAction.h"
#include "Face_Config.h"
#include "Variable.h"

void BlueTooth_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);//开启GPIOB时钟
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3,ENABLE);//开启串口时钟

	//蓝牙
		GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_AF_PP;//复用推挽输出模式
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_10;//默认PB10是USART3_TX的复用
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOB,&GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_IPU;//复用上拉输入模式
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_11;//默认PB11是USART3_RX的复用
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOB,&GPIO_InitStructure);
	
	USART_InitTypeDef UASRT_InitStructure;//USART初始化
	UASRT_InitStructure.USART_BaudRate=115200;//波特率115200（模块出厂多为9600，务必先统一）
	UASRT_InitStructure.USART_HardwareFlowControl=USART_HardwareFlowControl_None;//不需要硬件流控制
	UASRT_InitStructure.USART_Mode=USART_Mode_Rx|USART_Mode_Tx;//接受与发送均打开
	UASRT_InitStructure.USART_Parity=USART_Parity_No;//不需要奇偶校验
	UASRT_InitStructure.USART_StopBits=USART_StopBits_1;//停止位为1
	UASRT_InitStructure.USART_WordLength=USART_WordLength_8b;//字长8位
	USART_Init(USART3,&UASRT_InitStructure);
		
	/*【重要】使能接收中断之前，先清掉 RX 线上可能残留的噪声字节和溢出标志。
	   否则一使能中断，就会立刻把上电期间积累的一个"幽灵字节"当成命令执行。
	   对连续旋转舵机来说，一个误判字节就足以让腿一直转下去（见 PetAction.c）。*/
	if(USART_GetFlagStatus(USART3,USART_FLAG_ORE)!=RESET)
	{
		(void)USART_ReceiveData(USART3);
	}
	if(USART_GetFlagStatus(USART3,USART_FLAG_RXNE)!=RESET)
	{
		(void)USART_ReceiveData(USART3);
	}
	
	USART_ITConfig(USART3,USART_IT_RXNE,ENABLE);//蓝牙接收中断配置，也就是如果接送到消息就直接中断
	
	//中断
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);//分组2

  //蓝牙中断配置
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel=USART3_IRQn;//特定的通道
	NVIC_InitStructure.NVIC_IRQChannelCmd=ENABLE;//通道使能
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=2;//抢占式优先级为2
	NVIC_InitStructure.NVIC_IRQChannelSubPriority=2;//响应优先级为2
	NVIC_Init(&NVIC_InitStructure);
	
	USART_Cmd(USART3,ENABLE);//USART3使能打开
}

void USART3_IRQHandler(void)
{
	if(USART_GetITStatus(USART3,USART_IT_RXNE)==SET)//如果接受到
	{
		RxData_Blue=USART_ReceiveData(USART3);
		/*动作判断*/
	  PetAction_Judgmengt(1);
		
		USART_ClearITPendingBit(USART3,USART_IT_RXNE);
	}
}
