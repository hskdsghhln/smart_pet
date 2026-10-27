#include "stm32f10x.h"                  // Device header
#include "PWM.h"
#include "PetAction.h"
#include "Face_Config.h"
#include "Variable.h"
#include "Weather.h"

/*【单字节指令的"间隙过滤"】
  蓝牙模块（尤其 AT 固件的）在手机连上 / 断开时会往串口吐一串状态字符，
  比如 "CONNECTED" / "DISCONNECTED"。这些字母里就藏着合法指令码：
  'C'=0x43 打招呼、'D'=0x44 开灯、'I'=0x49 拉伸，而 0x52~0x55 是
  'R' 'S' 'T' 'U' —— 正好对应摇摆 27/16 秒，于是一连接蓝牙就自己摇起来了。

  合法指令永远是"单个字节"，模块状态串是"一串字节"，所以按 20ms 一个窗口
  统计：一个窗口里只来 1 个字节才当指令执行，来 0 个或 ≥2 个都放过。
  ISR 里只入缓冲，真正的判定放在 BlueTooth_Poll()（20ms 由 TIM4 中断调用），
  这样每条指令最多延迟 20ms，人感觉不出来。 */
static volatile uint8_t Bt_RxCnt  = 0;   /*这一窗口收到了几个字节*/
static volatile uint8_t Bt_RxByte = 0;   /*这一窗口最后一个字节*/

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

void BlueTooth_SendByte(uint8_t b)
{
	/* 等发送寄存器空再写，否则会覆盖还没移出去的字节。
	   TXE 正常情况下一进来就是置位的，只有上一个字节还没发完才会短暂等待
	   （115200 下一个字节 87us）。加计数上限是为了防止模块没接、
	   或 USART 配置异常时卡死在这里——这里可能在中断里被调用。*/
	uint16_t guard = 0;
	while((USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET) && (guard < 20000))
	{
		guard++;
	}
	USART_SendData(USART3, b);
}

void USART3_IRQHandler(void)
{
	uint8_t b;

	if(USART_GetITStatus(USART3,USART_IT_RXNE)==SET)//如果接受到
	{
		b=(uint8_t)USART_ReceiveData(USART3);
		/*【天气帧】以 AA 55 开头的帧由 Weather 模块整帧收集并消费，
		  不能进入下面的单字节指令通道，否则帧里的数据字节会被当成命令误触发动作*/
		if(!Weather_OnByte(b))
		{
			/*这里只入缓冲，不直接执行——执行时机见 BlueTooth_Poll()*/
			Bt_RxByte=b;
			if(Bt_RxCnt<0xFFU)
			{
				Bt_RxCnt++;
			}
			/*借这个全局给"标定模式"用：SERVO_TEST_MODE==2 时不启动
			  调度器（TIM4 不开），Trim_Run() 就是轮询 RxData_Blue 的，
			  所以这里照旧把字节写进去，不然后台标定收不到指令。*/
			RxData_Blue=b;
		}
		USART_ClearITPendingBit(USART3,USART_IT_RXNE);
	}
}

/**
  * @brief  单字节指令的判定与分发（每 20ms 调用一次）
  * @note   由 TIM4 的 1ms 中断分频调用（见 TS_Port.c）。
  *         放在定时器中断而不是任务里，是因为动作函数是阻塞式的：
  *         如果放在任务链里，摇摆 26 秒期间新指令要等动作结束才能被处理。
  */
void BlueTooth_Poll(void)
{
	uint8_t n;
	uint8_t b;
	uint32_t primask;

	/*取数和执行都放在临界区里：PetAction_Judgmengt() 读的是全局 RxData_Blue，
	  中间要是被 USART3 中断插进来改写，就会执行成别的指令。
	  （蓝牙分支里不会发串口数据，所以这段只有几个赋值，关中断的时间很短）*/
	primask=__get_PRIMASK();
	__disable_irq();
	n=Bt_RxCnt;
	b=Bt_RxByte;
	Bt_RxCnt=0;
	if(n==1U)
	{
		RxData_Blue=b;
		PetAction_Judgmengt(1);
	}
	__set_PRIMASK(primask);

	/*n==0   : 这一窗口没有字节，什么都不做
	  n>=2   : 一窗口里来了好几个字节，是模块状态串或线路干扰，整簇丢弃*/
}
