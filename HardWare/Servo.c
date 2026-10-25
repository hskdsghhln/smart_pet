#include "stm32f10x.h"                  // Device header

void Servo_Init(void)
{
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2,ENABLE);//开启TIM2时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);//开启GPIOA时钟

	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_AF_PP;//复用推挽输出模式
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_2|GPIO_Pin_3;//默认PA0是TIM2通道1的复用，PA1是TIM2通道2的复用所以开启这俩IO口...
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_InitStructure);
	
	TIM_InternalClockConfig(TIM2);//TIM2切换为内部定时器
	
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	TIM_TimeBaseInitStructure.TIM_ClockDivision=TIM_CKD_DIV1;//不分频
	TIM_TimeBaseInitStructure.TIM_CounterMode=TIM_CounterMode_Up;//向上计数
	TIM_TimeBaseInitStructure.TIM_Period=20000-1;
	TIM_TimeBaseInitStructure.TIM_Prescaler=72-1;
	TIM_TimeBaseInitStructure.TIM_RepetitionCounter=0;
	TIM_TimeBaseInit(TIM2,&TIM_TimeBaseInitStructure);
	
	TIM_OCInitTypeDef TIM_OCInitStructure;
	TIM_OCStructInit(&TIM_OCInitStructure);
	TIM_OCInitStructure.TIM_OCMode=TIM_OCMode_PWM1;//输出比较模式采用PWM1
	TIM_OCInitStructure.TIM_OCPolarity=TIM_OCPolarity_High;
	TIM_OCInitStructure.TIM_OutputState=TIM_OutputState_Enable;
	/*初值取 1500us = 90° 中位。对 SG90 这类 180° 位置舵机来说，上电就输出中位
	  才能让它有个确定的起始姿态；绝对不能取 0 —— CCR=0 在 PWM1 模式下是恒定低电平，
	  舵机收不到有效脉宽，会失控乱转，直到第一次写入 1500us 才停下。*/
	TIM_OCInitStructure.TIM_Pulse=1500;//初始化CCR的值为中位(90度)
	TIM_OC1Init(TIM2,&TIM_OCInitStructure);//TIM2复用通道1开启
	TIM_OC2Init(TIM2,&TIM_OCInitStructure);//TIM2复用通道2开启
	TIM_OC3Init(TIM2,&TIM_OCInitStructure);//TIM2复用通道3开启
	TIM_OC4Init(TIM2,&TIM_OCInitStructure);//TIM2复用通道4开启
	
	TIM_Cmd(TIM2,ENABLE);//使能TIM2
}


void PWM_SetCompare1(uint16_t Compare)
{
	TIM_SetCompare1(TIM2, Compare);//设置CCR1的值		
}

void PWM_SetCompare2(uint16_t Compare)
{
	TIM_SetCompare2(TIM2, Compare);//设置CCR2的值
}

void PWM_SetCompare3(uint16_t Compare)
{
	TIM_SetCompare3(TIM2, Compare);//设置CCR3的值
}

void PWM_SetCompare4(uint16_t Compare)
{
	TIM_SetCompare4(TIM2, Compare);//设置CCR4的值
}


/*==============================================================================
 * 四条腿的中位微调（单位：度）
 *==============================================================================
 * 用途：补偿舵盘齿位装不准导致的"代码里的 90° 不是你想要的直立姿态"。
 *       所有舵机机器人出厂都要做这一步——机械上做不到绝对精确，只能用软件补。
 *
 * 叠加位置在【镜像换算之前】，所以对 Servo_Angle2/4 同样有效，
 * 四条腿的正负方向是一致的（正数 = 往角度变大的方向偏）。
 *
 * 换算：1 度 ≈ 11.1us（2000us / 180°）。建议范围 ±40°，
 *       再大说明装配偏差过大，应该重新装舵盘而不是硬拉。
 *
 * 怎么定值：把 main.c 里的 SERVO_TEST_MODE 改成 2，用蓝牙的 1~4 选腿、
 *           +/- 增减，屏幕会实时显示度数；四条腿都调正之后，
 *           把读到的数值抄到这里，再把模式改回 0 重新编译。
 *============================================================================*/
#define SERVO_TRIM_1      +21      /* 左前 S1 */
#define SERVO_TRIM_2      +22      /* 右前 S2 */
#define SERVO_TRIM_3      0      /* 左后 S3 */
#define SERVO_TRIM_4     -20      /* 右后 S4 */

/* 运行时微调表，下标 1~4 对应四条腿，初值来自上面的宏 */
static int8_t Servo_Trim[5] = { 0, SERVO_TRIM_1, SERVO_TRIM_2, SERVO_TRIM_3, SERVO_TRIM_4 };

/*设置某条腿的中位微调（标定模式用，调好后请写回上面的宏）*/
void Servo_SetTrim(uint8_t Ch, int8_t Deg)
{
	if((Ch >= 1U) && (Ch <= 4U))
	{
		Servo_Trim[Ch] = Deg;
	}
}

/*读取某条腿的中位微调*/
int8_t Servo_GetTrim(uint8_t Ch)
{
	if((Ch >= 1U) && (Ch <= 4U))
	{
		return Servo_Trim[Ch];
	}
	return 0;
}

/**
  * @brief  角度(+微调) -> 脉宽(us)
  * @note   限幅到 0~180°（即 500~2500us），防止微调把脉宽推到量程外把舵机顶死。
  */
static uint16_t Servo_Pulse(uint8_t Ch, float Angle)
{
	float a = Angle + (float)Servo_Trim[Ch];

	if(a < 0.0f)   { a = 0.0f;   }
	if(a > 180.0f) { a = 180.0f; }

	return (uint16_t)(a / 180.0f * 2000.0f + 500.0f);
}

void Servo_Angle1(float Angle)//左上
{
	PWM_SetCompare1(Servo_Pulse(1U, Angle));
}

void Servo_Angle2(float Angle)//右上
{
	/*镜像：脉宽 = 3000 - 正向脉宽，等价于原来的 (180-Angle)/180*2000+500*/
	PWM_SetCompare2((uint16_t)(3000U - Servo_Pulse(2U, Angle)));
}

void Servo_Angle3(float Angle)//左下
{
	PWM_SetCompare3(Servo_Pulse(3U, Angle));
}

void Servo_Angle4(float Angle)//右下
{
	/*镜像：同上*/
	PWM_SetCompare4((uint16_t)(3000U - Servo_Pulse(4U, Angle)));
}
