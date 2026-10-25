#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "OLED.h"
#include "BlueTooth.h"
#include "Servo.h"
#include "PetAction.h"
#include "Face_Config.h"
#include "PWM.h"
#include "PowerDetection.h"
#include "Variable.h"
#include "Led_Breathing.h"
#include "Timer.h"
#include "Voice.h"
#include "SchedulerApp.h"               // 任务调度（TaskScheduler 移植层，C 接口）

/***************************************************************************************
  * 本程序由博主我Sngels_wyh创建并免费开源共享
  * 你可以任意查看、使用和修改，并应用到自己的项目之中
  * 
  * 程序名称：				基于STM32F103C8T6单片机的桌面宠物小项目
  * 此程序更新时间：			2026.3.23
  * 
  * 主要在立创开源平台、CSDN网站、哔哩哔哩、抖音分享自己的作品。
	*	相关的资料可以去立创开源硬件平台网站上搜索,作品附件放置更新网盘，如有更新可方便快速查看
	* 地址:https://oshwhub.com/sngelswyh/stm32-smart-desktop-pet
  ***************************************************************************************/

/***************************************************************************************
  * 【TaskScheduler 移植说明】
  *
  * 本工程已移植 arkhipenko/TaskScheduler（v4.1.0，BSD-3-Clause）作为协作式任务调度器：
  *     TaskScheduler/            库源码 + STM32F103 移植层（TIM4 1ms 时基）
  *     User/SchedulerApp.cpp     任务定义（原来的主循环工作被改造成任务）
  *     User/SchedulerApp.h       对外暴露的 C 接口
  *
  * 分工：
  *     主循环里的 Face_Config() / PetAction_Perform()  =>  变成调度器里的两个任务
  *     TIM3 中断里的 GetCur_Power() / LED_Breathing()  =>  保持在中断里（硬实时）
  *
  * 为什么电量检测和呼吸灯没有搬进任务里：TaskScheduler 是协作式调度器，不能抢占
  * 正在执行的代码；而 PetAction_Perform() 内部是阻塞式的（连续 Delay_ms、甚至无限
  * 循环），动作执行期间任务链上排在后面的任务会被饿死。呼吸灯与电量采样必须保持
  * 严格周期，所以留在定时器中断里才是正确做法。
  ***************************************************************************************/

/* TIM3 更新中断的分频系数：
 * TIM3 的 TIM_Period=20000 同时决定了舵机 PWM 必须为 50Hz（20ms 周期），
 * 因此不能通过改 TIM3 周期来降低中断频率，只能在中断里分频。
 * 取 2 => 周期任务实际每 40ms 执行一次（呼吸灯步长已在 Led_Breathing.c 中同步放大）。 */
#define PERIODIC_TICK_DIV       2

/*#####################################################################################
 *【测试开关】编译期选择固件功能
 *
 *   SERVO_TEST_MODE = 0  ->  正常运行程序（桌面宠物功能）
 *   SERVO_TEST_MODE = 1  ->  SG90 全量程扇扫，检查每条腿的行程/卡滞/干涉
 *   SERVO_TEST_MODE = 2  ->  中位微调标定（蓝牙可调），校正舵盘装配偏差
 *
 * 正常工作请保持 0。做完测试/标定后改回 0 重新编译，测试代码不会被编译进去。
 *###################################################################################*/
#define SERVO_TEST_MODE         0

#if (SERVO_TEST_MODE != 0)

/* 腿编号 -> 屏幕显示名（OLED 字库只有 ASCII，不要写中文）*/
static char *const ServoTest_LegName[5] =
{
	"----",
	"S1 LF",   /* 左前 */
	"S2 RF",   /* 右前 */
	"S3 LR",   /* 左后 */
	"S4 RR"    /* 右后 */
};

#endif /* SERVO_TEST_MODE != 0 */

/*#####################################################################################
 * 模式 1：SG90 全量程扇扫
 *###################################################################################*/
#if (SERVO_TEST_MODE == 1)

/*==============================================================================
 * SG90 全量程扇扫测试
 *
 * 【目的】
 * 1. 看每条腿的实际可用行程到哪结束（很多 SG90 到 2400us 左右就到机械限位了，
 *    再往上只会持续堵转、发热、嗡嗡响）。
 * 2. 看中间有没有卡滞点（连杆干涉、线束拉紧）。
 * 3. 看左右腿的镜像方向是否对称（最后一段四腿一起扫，走的是正式代码里的
 *    Servo_Angle1~4，带镜像换算，左右应当摆出对称姿态）。
 *
 * 【怎么读数】
 * 屏幕上会显示当前输出的脉宽（us）。软尺子不好量角度时，就这样记：
 *      从 500 一路上扫，记下"腿开始不动、但声音还在响"的那个数值 = 该侧限位
 *      再从上往下扫，记下另一侧的限位
 * 两边限位之间的差值就是这个舵机的真实可用行程。
 *
 * 【注意】扫描时屏幕不是每步都刷新——OLED 是软件 I2C，一次全屏刷新约 20ms，
 * 每步都刷会把扫描拖得很慢。这里每 10 步（250us）刷一次，足够定位限位点。
 *============================================================================*/

#define SWEEP_MIN_US         500U     /* 扫描下限（0°）   */
#define SWEEP_MAX_US        2500U     /* 扫描上限（180°） */
#define SWEEP_STEP_US         25U     /* 步长：2000us/180° => 25us 约合 2.25° */
#define SWEEP_STEP_MS         30U     /* 每步停留时间     */
#define SWEEP_HOLD_MS        400U     /* 关键位置停留时间 */
#define SWEEP_GAP_MS         800U     /* 换下一条腿的间隔 */
#define SWEEP_START_MS      5000U     /* 上电后等待时间（留时间摆腿）*/
#define SWEEP_REFRESH_STEPS   10U     /* 每扫多少步刷新一次屏幕 */

/**
  * @brief  给指定通道输出原始脉宽（不做镜像换算，用于量程标定）
  */
static void ServoTest_SetPulse(uint8_t ch, uint16_t us)
{
	switch(ch)
	{
		case 1: PWM_SetCompare1(us); break;
		case 2: PWM_SetCompare2(us); break;
		case 3: PWM_SetCompare3(us); break;
		case 4: PWM_SetCompare4(us); break;
		default: break;
	}
}

/**
  * @brief  四条腿全部回到中位 1500us（90°）
  */
static void ServoTest_AllStop(void)
{
	PWM_SetCompare1(1500);
	PWM_SetCompare2(1500);
	PWM_SetCompare3(1500);
	PWM_SetCompare4(1500);
}

/**
  * @brief  显示当前测试进度
  * @note   不用 OLED_Printf：它会把 printf 引擎链接进来（约 6KB），
  *         这里只需要固定字符串 + 一个 4 位数，ShowString/ShowNum 足够。
  */
static void ServoTest_Show(uint8_t leg, char *phase, uint16_t us)
{
	OLED_Clear();
	OLED_ShowString(0,  0, "SG90 SWEEP TEST", OLED_6X8);
	OLED_ShowString(0, 14, ServoTest_LegName[leg], OLED_6X8);
	OLED_ShowString(0, 28, phase, OLED_6X8);
	OLED_ShowString(0, 42, "pulse:", OLED_6X8);
	OLED_ShowNum(48, 42, us, 4, OLED_6X8);
	OLED_ShowString(80, 42, "us", OLED_6X8);
	OLED_Update();
}

/**
  * @brief  对一条腿做完整程扇扫
  */
static void ServoTest_SweepLeg(uint8_t leg)
{
	uint16_t us;
	uint16_t n;

	/* 先让所有腿回中位，被测腿从 90° 出发 */
	ServoTest_AllStop();
	ServoTest_Show(leg, "mid 1500us", 1500U);
	Delay_ms(SWEEP_HOLD_MS);

	/*---------- 正向：500us -> 2500us ----------*/
	n=0;
	for(us=SWEEP_MIN_US; us<=SWEEP_MAX_US; us+=SWEEP_STEP_US)
	{
		ServoTest_SetPulse(leg, us);

		if((n % SWEEP_REFRESH_STEPS)==0U)
		{
			ServoTest_Show(leg, "sweep up", us);
		}
		n++;

		Delay_ms(SWEEP_STEP_MS);
	}

	/*---------- 反向：2500us -> 500us ----------*/
	n=0;
	for(us=SWEEP_MAX_US; us>=SWEEP_MIN_US; us-=SWEEP_STEP_US)
	{
		ServoTest_SetPulse(leg, us);

		if((n % SWEEP_REFRESH_STEPS)==0U)
		{
			ServoTest_Show(leg, "sweep down", us);
		}
		n++;

		Delay_ms(SWEEP_STEP_MS);

		/*us 是 uint16_t，减到下限就立刻跳出，避免下溢成 65535 死循环*/
		if(us==SWEEP_MIN_US) { break; }
	}

	/*---------- 在三个关键位置各停一下，方便目测 ----------*/
	ServoTest_SetPulse(leg, SWEEP_MAX_US);
	ServoTest_Show(leg, "hold MAX", SWEEP_MAX_US);
	Delay_ms(SWEEP_HOLD_MS);

	ServoTest_SetPulse(leg, 1500U);
	ServoTest_Show(leg, "hold MID", 1500U);
	Delay_ms(SWEEP_HOLD_MS);

	ServoTest_SetPulse(leg, SWEEP_MIN_US);
	ServoTest_Show(leg, "hold MIN", SWEEP_MIN_US);
	Delay_ms(SWEEP_HOLD_MS);

	/*---------- 回中位，换下一条腿 ----------*/
	ServoTest_SetPulse(leg, 1500U);
	ServoTest_Show(leg, "back to MID", 1500U);
	Delay_ms(SWEEP_GAP_MS);
}

/**
  * @brief  四条腿一起扫 30°~150°，检查左右镜像是否对称
  * @note   这里走正式的 Servo_Angle1~4，带镜像换算，
  *         所以正常情况下左右腿应当摆出对称的姿态。
  */
static void ServoTest_SweepAll(void)
{
	int a;

	ServoTest_AllStop();
	OLED_Clear();
	OLED_ShowString(0,  0, "SG90 SWEEP TEST", OLED_6X8);
	OLED_ShowString(0, 14, "ALL 4 LEGS", OLED_6X8);
	OLED_ShowString(0, 28, "30deg - 150deg", OLED_6X8);
	OLED_ShowString(0, 42, "check symmetry", OLED_6X8);
	OLED_ShowString(0, 52, "and interference", OLED_6X8);
	OLED_Update();
	Delay_ms(SWEEP_HOLD_MS);

	for(a=30;a<=150;a++)
	{
		Servo_Angle1((float)a);
		Servo_Angle2((float)a);
		Servo_Angle3((float)a);
		Servo_Angle4((float)a);
		Delay_ms(20);
	}
	for(a=150;a>=30;a--)
	{
		Servo_Angle1((float)a);
		Servo_Angle2((float)a);
		Servo_Angle3((float)a);
		Servo_Angle4((float)a);
		Delay_ms(20);
	}

	ServoTest_AllStop();
	Delay_ms(SWEEP_HOLD_MS);
}

/**
  * @brief  执行一遍完整测试（阻塞式，跑完返回）
  */
static void ServoTest_Run(void)
{
	uint8_t leg;

	ServoTest_AllStop();

	OLED_Clear();
	OLED_ShowString(0,  0, "SG90 SWEEP TEST", OLED_6X8);
	OLED_ShowString(0, 12, "each leg scans", OLED_6X8);
	OLED_ShowString(0, 22, "500 - 2500us", OLED_6X8);
	OLED_ShowString(0, 34, "watch for:", OLED_6X8);
	OLED_ShowString(0, 44, " bind / buzz", OLED_6X8);
	OLED_ShowString(0, 54, "start in 5s", OLED_6X8);
	OLED_Update();

	Delay_ms(SWEEP_START_MS);

	/* 四条腿依次扫描 */
	for(leg=1U; leg<=4U; leg++)
	{
		ServoTest_SweepLeg(leg);
	}

	/* 四腿一起扫，检查镜像对称和干涉 */
	ServoTest_SweepAll();

	ServoTest_AllStop();

	OLED_Clear();
	OLED_ShowString(0,  0, "SWEEP DONE", OLED_6X8);
	OLED_ShowString(0, 16, "all legs back", OLED_6X8);
	OLED_ShowString(0, 26, "to 90 deg", OLED_6X8);
	OLED_ShowString(0, 42, "reset to rerun", OLED_6X8);
	OLED_Update();
}

#endif /* SERVO_TEST_MODE == 1 */

/*#####################################################################################
 * 模式 2：中位微调标定
 *###################################################################################*/
#if (SERVO_TEST_MODE == 2)

/*==============================================================================
 * 中位微调标定模式
 *
 * 【为什么需要】
 * 舵机的舵盘是有齿的，装上去时很难刚好卡在"代码里的 90° = 你要的直立姿态"。
 * 齿距通常 5~10°，所以四条腿几乎不可能一次装准。标准做法不是反复拆装，
 * 而是每条腿加一个软件偏移（trim）把它补回来。
 *
 * 【怎么用】
 * 上电后四条腿都停在 90°（叠加当前 trim），用蓝牙串口发单字节 ASCII：
 *
 *      1 2 3 4   选择要调的腿：左前 / 右前 / 左后 / 右后
 *      +         当前腿 trim +1 度
 *      -         当前腿 trim -1 度
 *      0         当前腿 trim 清零（重新开始调）
 *
 * 屏幕上实时显示当前腿和它的 trim 值。四条腿都调正之后，
 * 把读到的四个数字抄进 Servo.c 的 SERVO_TRIM_1~4，再把模式改回 0 重编。
 *
 * 【注意】trim 只存在 RAM 里，复位就回到 Servo.c 里的初值——
 *         所以调好后一定要抄下来写死，否则下次上电白调。
 *============================================================================*/

#define TRIM_LIMIT_DEG      40      /* 微调上限（度），到这个值说明装配偏差过大 */

/**
  * @brief  刷新标定界面
  */
static void Trim_Show(uint8_t Leg)
{
	int8_t t = Servo_GetTrim(Leg);

	OLED_Clear();
	OLED_ShowString(0,  0, "TRIM CALIBRATE", OLED_6X8);
	OLED_ShowString(0, 14, ServoTest_LegName[Leg], OLED_6X8);

	OLED_ShowString(0, 30, "trim:", OLED_6X8);
	if(t < 0)
	{
		OLED_ShowString(40, 30, "-", OLED_6X8);
		OLED_ShowNum(50, 30, (uint32_t)(-t), 2, OLED_6X8);
	}
	else
	{
		OLED_ShowString(40, 30, "+", OLED_6X8);
		OLED_ShowNum(50, 30, (uint32_t)t, 2, OLED_6X8);
	}
	OLED_ShowString(70, 30, "deg", OLED_6X8);

	OLED_ShowString(0, 46, "1-4  +/-  0=zero", OLED_6X8);
	OLED_Update();
}

/**
  * @brief  四条腿回到 90°（trim 由 Servo.c 内部叠加）
  */
static void Trim_Stand(void)
{
	Servo_Angle1(90.0f);
	Servo_Angle2(90.0f);
	Servo_Angle3(90.0f);
	Servo_Angle4(90.0f);
}

/**
  * @brief  标定主循环（不返回）
  */
static void Trim_Run(void)
{
	uint8_t  Leg = 1U;
	uint16_t Key;
	int8_t   t;

	Trim_Stand();
	Trim_Show(Leg);

	while(1)
	{
		Key = RxData_Blue;

		if(Key != 0U)
		{
			/*消费掉这个字节，这样连续按同一个键也能再次识别*/
			RxData_Blue = 0U;

			if((Key >= '1') && (Key <= '4'))
			{
				Leg = (uint8_t)(Key - '1') + 1U;
			}
			else if(Key == '+')
			{
				t = Servo_GetTrim(Leg);
				if(t < (int8_t)TRIM_LIMIT_DEG) { Servo_SetTrim(Leg, (int8_t)(t + 1)); }
			}
			else if(Key == '-')
			{
				t = Servo_GetTrim(Leg);
				if(t > (int8_t)(-TRIM_LIMIT_DEG)) { Servo_SetTrim(Leg, (int8_t)(t - 1)); }
			}
			else if(Key == '0')
			{
				Servo_SetTrim(Leg, 0);
			}
			else
			{
				/*其它按键忽略*/
			}

			Trim_Stand();
			Trim_Show(Leg);
		}
	}
}

#endif /* SERVO_TEST_MODE == 2 */

int main(void)
{	
	GetBattery_Init();//电量检测初始化
	Servo_Init();//舵机初始化
	OLED_Init();//OLED初始化
	Timer_Init();//定时器初始化
	BlueTooth_Init();//蓝牙初始化
	Voice_Init();//语音初始化

#if (SERVO_TEST_MODE == 0)
	/*==================== 正常运行模式 ====================*/
	OLED_ShowImage(0,0,128,64,Face_eyes);
	OLED_Update();

	/*任务调度初始化（启动 1ms 时基 + 使能任务）。必须放在最后：
	  它会配置 TIM4 的中断优先级，需要 NVIC 优先级分组已由上面的串口初始化设定好*/
	SchedulerApp_Init();

	while(1)
	{	
		/*执行一次任务调度：
		    - OLED 表情(电量) 任务，30ms 周期
		    - 舵机动作 任务，每个调度 pass 执行一次
		  原来直接写在 while 里的 Face_Config()/PetAction_Perform() 已迁移过去*/
		SchedulerApp_Run();
	}
#elif (SERVO_TEST_MODE == 1)
	/*==================== SG90 扇扫测试模式 ====================
	  注意：这里【不】调用 SchedulerApp_Init()，所以动作任务不会运行，
	        避免 PetAction 里的动作和测试程序抢着写同一个 TIM2 通道。*/
	ServoTest_Run();

	/*测试跑完就停在这里，四条腿保持 1500us（90°），不再动作*/
	while(1)
	{
		__NOP();
	}
#else
	/*==================== 中位微调标定模式 ====================
	  同样不启动调度器；Trim_Run() 内部是死循环，不会返回。*/
	Trim_Run();
#endif
}

/*每20ms进一次中断；经 PERIODIC_TICK_DIV 分频后，周期任务实际每40ms执行一次*/
void TIM3_IRQHandler(void)
{
	static uint8_t TickDiv = 0;

	if(TIM_GetITStatus(TIM3,TIM_IT_Update)==SET)
	{	
		if(++TickDiv >= PERIODIC_TICK_DIV)
		{
			TickDiv = 0;

			/*电量检测*/
			GetCur_Power();
			/*呼吸灯*/
			LED_Breathing();
		}

		TIM_ClearITPendingBit(TIM3,TIM_IT_Update);
	}
}
