/**
  ******************************************************************************
  * @file    TS_Port.c
  * @brief   TaskScheduler 的 STM32F103 移植层实现（TIM4 1ms 时基）
  *
  * 硬件占用：
  *   TIM4 -> 1MHz 计数 / 1ms 更新中断 / NVIC 抢占优先级 3（最低，见下方说明）
  *
  * 为什么不用别的定时器：
  *   TIM2  : 被 PWM.c / Servo.c 用作舵机 PWM
  *   TIM3  : 被 Timer.c 用作 50Hz 舵机 PWM，其更新中断同时驱动呼吸灯与电量检测
  *   SysTick: 被 Delay.c 的 Delay_us() 反复重配置（用完即关），不能作为连续时基
  *   TIM1  : 高级定时器，留给将来需要互补输出/死区时使用
  ******************************************************************************
  */

#include "stm32f10x.h"
#include "TS_Port.h"

/* 20ms 一次的蓝牙指令判定。放在这里而不是任务链里，是因为舵机动作是
   阻塞式实现（摇摆最长 27 秒），任务链上的任务在这期间全都被饿死。
   见 BlueTooth_Poll() 的说明。 */
#include "BlueTooth.h"

/*==============================================================================
 * 配置
 *============================================================================*/

/** TIM4 计数频率：72MHz / 72 = 1MHz（APB1 为 36MHz，但定时器时钟是 APB1 的 2 倍） */
#define TS_TIM4_PRESCALER       (72 - 1)

/** 1MHz / 1000 = 1kHz，即 1ms 一次更新中断 */
#define TS_TIM4_PERIOD          (1000 - 1)

/**
  * TIM4 中断优先级。
  * 本工程使用 NVIC_PriorityGroup_2（2 位抢占 + 2 位响应），抢占优先级取值 0~3：
  *   USART1(语音) 抢占 1 / 响应 1
  *   USART3(蓝牙) 抢占 2 / 响应 2
  * 时基只做一次自增，应让串口中断能抢占它，所以取最低的 3。
 */
#define TS_TIM4_PREEMPTION_PRIORITY     3
#define TS_TIM4_SUB_PRIORITY            0

/*==============================================================================
 * 模块内变量
 *============================================================================*/

/** 已走过的整毫秒数，只在 TIM4 中断里自增 */
static volatile uint32_t s_tsTick = 0;

/** 蓝牙指令判定的 20ms 分频计数，只在 TIM4 中断里动 */
static volatile uint8_t  s_btPollDiv = 0;

#define TS_BT_POLL_DIV      20U

/*==============================================================================
 * 移植层实现
 *============================================================================*/

/**
  * @brief  初始化 1ms 时基（TIM4 更新中断）
  */
void TS_Port_Init(void)
{
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	NVIC_InitTypeDef        NVIC_InitStructure;

	/* 开启 TIM4 时钟（TIM4 挂在 APB1 上） */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

	/*
	 * 明确优先级分组（与 BlueTooth.c / Voice.c 中的设置保持一致）。
	 * 本工程统一使用分组 2：2 位抢占优先级 + 2 位响应优先级，
	 * 即抢占优先级取值范围为 0~3。
	 * 重复设置是幂等的，写在这里可以让本模块不依赖初始化顺序。
	 */
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

	s_tsTick = 0;

	/* 1MHz 计数、每 1000 个计数产生一次更新事件（1ms） */
	TIM_InternalClockConfig(TIM4);

	TIM_TimeBaseInitStructure.TIM_ClockDivision     = TIM_CKD_DIV1;
	TIM_TimeBaseInitStructure.TIM_CounterMode       = TIM_CounterMode_Up;
	TIM_TimeBaseInitStructure.TIM_Period            = TS_TIM4_PERIOD;
	TIM_TimeBaseInitStructure.TIM_Prescaler         = TS_TIM4_PRESCALER;
	TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM4, &TIM_TimeBaseInitStructure);

	/* 允许更新中断，并清掉初始化过程中可能产生的挂起标志 */
	TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
	TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);

	/* NVIC 配置 */
	NVIC_InitStructure.NVIC_IRQChannel                   = TIM4_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = TS_TIM4_PREEMPTION_PRIORITY;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority        = TS_TIM4_SUB_PRIORITY;
	NVIC_Init(&NVIC_InitStructure);

	TIM_Cmd(TIM4, ENABLE);
}

/**
  * @brief  毫秒级时基
  */
uint32_t TS_Millis(void)
{
	return s_tsTick;
}

/**
  * 读取微秒时允许的最大重试次数。
  * 正常情况下最多重试 1~2 次就能拿到一致的值，这里给足余量。
  */
#define TS_MICROS_MAX_RETRY     8U

/**
  * @brief  微秒级时基
  *
  * 实现要点（避免“进位瞬间”读出错值）：
  *   TIM4 的 CNT 在更新事件发生时被硬件清零，同时 SR 的 UIF 置位；
  *   而 s_tsTick 要等中断服务函数执行时才自增。
  *   于是在“CNT 已清零、s_tsTick 还没加”的这段窗口里，
  *   (s_tsTick * 1000 + CNT) 会比真实值小 1000us。
  *
  *   处理办法：读之前和读之后都检查一次 UIF，并核对 s_tsTick 有没有被改动。
  *   只要有任何一次不一致（说明有尚未被中断处理掉的进位），就整体重读。
  *   中断服务函数里是“先自增 s_tsTick、后清 UIF”，
  *   因此 UIF 为 0 就一定意味着 s_tsTick 已经和 CNT 对齐。
  *
  *   【为什么重试必须有上限】
  *   如果本函数是在抢占优先级高于 TIM4 的中断里、或者全局关中断期间被调用，
  *   TIM4 中断根本得不到执行，UIF 就永远不会被清掉。
  *   无界的 while 会在这里死等 → 整个系统挂死。
  *   所以改成最多重试 TS_MICROS_MAX_RETRY 次，之后返回尽力而为的读数
  *   （最坏误差 1ms，对调度统计类用途完全够用），绝不阻塞调用者。
  */
uint32_t TS_Micros(void)
{
	uint32_t tick;
	uint32_t count;
	uint32_t retry;
	ITStatus flagBefore;
	ITStatus flagAfter;

	for (retry = 0U; retry < TS_MICROS_MAX_RETRY; retry++)
	{
		flagBefore = TIM_GetITStatus(TIM4, TIM_IT_Update);
		tick       = s_tsTick;
		count      = TIM4->CNT;
		flagAfter  = TIM_GetITStatus(TIM4, TIM_IT_Update);

		if ((flagBefore == RESET) && (flagAfter == RESET) && (tick == s_tsTick))
		{
			return (tick * 1000U) + count;
		}
	}

	/* 到这里说明 TIM4 中断一直没机会执行，放弃精确对齐，直接返回近似值 */
	return (s_tsTick * 1000U) + TIM4->CNT;
}

/**
  * @brief  协作式让步点
  */
void TS_Yield(void)
{
	__NOP();
}

/**
  * @brief  TIM4 更新中断服务函数：维护 1ms 软件计数器
  * @note   名字必须是启动文件 startup_stm32f10x_md.s 中弱定义的 TIM4_IRQHandler，
  *         否则会链接到启动文件里的死循环默认实现。
  */
void TIM4_IRQHandler(void)
{
	if (TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET)
	{
		/* 先自增再清标志：这是 TS_Micros() 正确性的前提，不要调换顺序 */
		s_tsTick++;

		/* 每 20ms 判定一次蓝牙指令（单字节 + 静默窗口过滤） */
		if (++s_btPollDiv >= TS_BT_POLL_DIV)
		{
			s_btPollDiv = 0;
			BlueTooth_Poll();
		}

		TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
	}
}
