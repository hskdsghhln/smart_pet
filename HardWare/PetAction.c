#include "stm32f10x.h"                  // Device header
#include "Servo.h"
#include "Delay.h"
#include "BlueTooth.h"
#include "Variable.h"
#include "Timer.h"
#include "Weather.h"
#include "Face_Config.h"

/*==============================================================================
 * 本文件按「180° 位置舵机（SG90）」重写
 *==============================================================================
 * 语义：Servo_AngleN(角度) 就是把腿转到这个角度并停在那儿，舵机自己闭环定位；
 *       Delay_ms(...) 只是给它留出"转过去"的时间，不需要持续刷新。
 *
 * 角度约定（对应 Servo.c 里的映射）：
 *       0°  ->  500us    腿向前进方向甩到底
 *      45°  -> 1000us
 *      90°  -> 1500us   站立中位
 *     135°  -> 2000us
 *     180°  -> 2500us   向后甩到底
 *
 * 注意 Servo_Angle2 / Servo_Angle4 在底层做了 (180-角度) 的镜像换算，这是为了
 * 适配左右腿镜像安装，所以同一个角度参数能让左右腿摆出对称的姿态。
 *
 * 结构约定：
 *   1、4 号舵机为一组（左前+右后，对角），2、3 号舵机为另一组（右前+左后，对角），
 *   两组对角腿交替摆动就构成了爬行步态。为便于阅读，这里封装成
 *   Legs_Pair1() / Legs_Pair2() / Legs_All() 三个辅助函数。
 *
 * 与上游原始版本相比，这里修掉了 4 个隐患（见各处的【修复】注释）：
 *   1. PAnumbers 是 uint16_t，减到 0 再减会回绕成 65535 -> 动作跑几十小时
 *   2. 蓝牙收到任何字节都会置 Sustainedmove=1 -> 一个乱码就能启动持续运动
 *   3. case 0x39 缺 break，会贯穿到 0x40 连带翻转摇尾巴
 *   4. 摇尾巴的 while(WeiBa_Bit) 会一直阻塞，导致表情任务被饿死
 *============================================================================*/

/*----------------------------------------------------------------------------
 * 动作辅助
 *---------------------------------------------------------------------------*/
/* 四条腿一起到某个角度 */
static void Legs_All(float Angle)
{
	Servo_Angle1(Angle);
	Servo_Angle2(Angle);
	Servo_Angle3(Angle);
	Servo_Angle4(Angle);
}

/* 对角组 1：左前(1) + 右后(4) */
static void Legs_Pair1(float Angle)
{
	Servo_Angle1(Angle);
	Servo_Angle4(Angle);
}

/* 对角组 2：右前(2) + 左后(3) */
static void Legs_Pair2(float Angle)
{
	Servo_Angle2(Angle);
	Servo_Angle3(Angle);
}

/*==============================================================================
 * 姿态动作
 *============================================================================*/

void Action_relaxed_getdowm(void)//放松的趴下
{
	Servo_Angle1(20);
	Servo_Angle2(20);
	Delay_ms(80);

	Servo_Angle3(160);
	Servo_Angle4(160);
	Delay_ms(80);

	/*摇尾巴判断*/
	Action_Mode=9;
}

void Action_upright(void)//站立
{
	Servo_Angle1(90);
	Servo_Angle2(90);
	Delay_ms(80);

	Servo_Angle3(90);
	Servo_Angle4(90);
	Delay_ms(80);

	/*摇尾巴判断*/
	Action_Mode=9;
}

void Action_upright2(void)//站立（起跳后的收腿，顺序相反）
{
	Servo_Angle3(90);
	Servo_Angle4(90);
	Delay_ms(80);

	Servo_Angle1(90);
	Servo_Angle2(90);
	Delay_ms(80);

	/*摇尾巴判断*/
	Action_Mode=9;
}

void Action_getdowm(void)//趴下
{
	Servo_Angle1(20);
	Servo_Angle2(20);
	Delay_ms(80);

	Servo_Angle3(20);
	Servo_Angle4(20);
	Delay_ms(80);

	/*摇尾巴判断*/
	Action_Mode=9;
}

void Action_sit(void)//坐下
{
	Servo_Angle1(90);
	Servo_Angle2(90);
	Delay_ms(80);

	Servo_Angle3(20);
	Servo_Angle4(20);
	Delay_ms(80);

	/*摇尾巴判断*/
	Action_Mode=9;
}

/*==============================================================================
 * 行走 / 转向
 *
 * 一个步态周期 = 两组对角腿交替摆动，最后一起回到中位。
 * SpeedDelay 决定每一拍的时长：SG90 大约 0.1s/60°，默认 200ms 足够走完 90°。
 *============================================================================*/

void Action_advance(void)//前进
{
	PAnumbers=Chongfunumber;

	while((Action_Mode==4) && (Sustainedmove || PAnumbers))
	{
		Face_Service();/*动作是阻塞的，每拍给表情一次刷新机会*/
		Legs_Pair2(45);
		Delay_ms(SpeedDelay);

		Legs_Pair1(135);
		Delay_ms(SpeedDelay);

		Legs_Pair2(90);
		Delay_ms(SpeedDelay);

		Legs_Pair1(90);
		Delay_ms(SpeedDelay);

		Legs_Pair1(45);
		Delay_ms(SpeedDelay);

		Legs_Pair2(135);
		Delay_ms(SpeedDelay);

		Legs_Pair1(90);
		Delay_ms(SpeedDelay);

		Legs_Pair2(90);
		Delay_ms(SpeedDelay);

		/*【修复】绝不减到 0 之后还减，否则 uint16_t 回绕成 65535*/
		if(PAnumbers) { PAnumbers--; }
	}

	Legs_All(90);

	if((Sustainedmove!=1) && (Action_Mode==4))
	{
		Action_Mode=2;
	}
}

void Action_back(void)//后退
{
	PAnumbers=Chongfunumber;

	while((Action_Mode==5) && (Sustainedmove || PAnumbers))
	{
		Face_Service();/*动作是阻塞的，每拍给表情一次刷新机会*/
		Legs_Pair2(135);
		Delay_ms(SpeedDelay);

		Legs_Pair1(45);
		Delay_ms(SpeedDelay);

		Legs_Pair2(90);
		Delay_ms(SpeedDelay);

		Legs_Pair1(90);
		Delay_ms(SpeedDelay);

		Legs_Pair1(135);
		Delay_ms(SpeedDelay);

		Legs_Pair2(45);
		Delay_ms(SpeedDelay);

		Legs_Pair1(90);
		Delay_ms(SpeedDelay);

		Legs_Pair2(90);
		Delay_ms(SpeedDelay);

		if(PAnumbers) { PAnumbers--; }
	}

	Legs_All(90);

	if((Sustainedmove!=1) && (Action_Mode==5))
	{
		Action_Mode=2;
	}
}

void Action_Lrotation(void)//向左旋转
{
	PAnumbers=Chongfunumber;

	while((Action_Mode==6) && (Sustainedmove || PAnumbers))
	{
		Face_Service();/*动作是阻塞的，每拍给表情一次刷新机会*/
		Servo_Angle2(45);
		Servo_Angle3(135);
		Delay_ms(SpeedDelay);

		Servo_Angle1(45);
		Servo_Angle4(135);
		Delay_ms(SpeedDelay);

		Legs_Pair2(90);
		Delay_ms(SpeedDelay);

		Legs_Pair1(90);
		Delay_ms(SpeedDelay);

		if(PAnumbers) { PAnumbers--; }
	}

	Legs_All(90);

	if((Sustainedmove!=1) && (Action_Mode==6))
	{
		Action_Mode=2;
	}
}

void Action_Rrotation(void)//向右旋转
{
	PAnumbers=Chongfunumber;

	while((Action_Mode==7) && (Sustainedmove || PAnumbers))
	{
		Face_Service();/*动作是阻塞的，每拍给表情一次刷新机会*/
		Servo_Angle1(45);
		Servo_Angle4(135);
		Delay_ms(SpeedDelay);

		Servo_Angle2(45);
		Servo_Angle3(135);
		Delay_ms(SpeedDelay);

		Legs_Pair1(90);
		Delay_ms(SpeedDelay);

		Legs_Pair2(90);
		Delay_ms(SpeedDelay);

		if(PAnumbers) { PAnumbers--; }
	}

	Legs_All(90);

	if((Sustainedmove!=1) && (Action_Mode==7))
	{
		Action_Mode=2;
	}
}

/*==============================================================================
 * 摇摆 / 摇尾巴
 *============================================================================*/

/* 摇摆
   SwingSeconds==0：按 SwingRepeatnumber 摇固定次数（0x37 的老行为）
   SwingSeconds>0 ：按时间摇，用 0x52~0x55 指定的秒数

   时间是自己累加出来的，不依赖任何定时器计数：每 Delay_ms(SwingDelay)
   就给 ElapsedMs 加一次，所以 0x39 改了摇摆速度之后，秒数依然准。
   一个来回 = (150-30+1)*2 = 242 步，默认 6ms/步 => 约 1.45 秒。 */
void Action_Swing(void)//摇摆
{
	uint16_t SwingNumber=SwingRepeatnumber;
	uint32_t ElapsedMs=0;                            /*已经摇过的毫秒数*/
	uint32_t TargetMs=(uint32_t)SwingSeconds*1000UL; /*0 = 不限时，按次数收尾*/
	uint16_t i;

	while(Action_Mode==8)
	{
		for(i=30;i<=150;i++)
		{
			if(Action_Mode!=8)break;
			Legs_All((float)i);
			Delay_ms(SwingDelay);
			Face_Service();/*摇摆是阻塞的，保证新表情一进来就能显示*/
			ElapsedMs+=SwingDelay;
			if(TargetMs && (ElapsedMs>=TargetMs))break;
		}
		if(Action_Mode!=8)break;
		if(TargetMs && (ElapsedMs>=TargetMs))break;

		for(i=150;i>=30;i--)
		{
			if(Action_Mode!=8)break;
			Legs_All((float)i);
			Delay_ms(SwingDelay);
			Face_Service();/*摇摆是阻塞的，保证新表情一进来就能显示*/
			ElapsedMs+=SwingDelay;
			if(TargetMs && (ElapsedMs>=TargetMs))break;
		}
		if(Action_Mode!=8)break;
		if(TargetMs && (ElapsedMs>=TargetMs))break;

		/*限时模式不看次数，摇够时间就结束；否则按 SwingRepeatnumber 收尾*/
		if(TargetMs==0)
		{
			if(SwingNumber) { SwingNumber--; }
			if(SwingNumber==0) break;
		}
	}

	Legs_All(90);

	if(Action_Mode==8)
	{
		Action_Mode=2;
	}

	SwingSeconds=0;/*时长用完就清掉，下次 0x37 恢复固定次数行为*/
}

void Action_SwingTail(void)//摇尾巴
{
	uint8_t n;

	/*不摇的时候把尾巴放回中位，然后立刻返回*/
	if(WeiBa_Bit==0)
	{
		WeiBa_Value=90;
		WeiBa_Dir=1;
		WServo_Angle(90);
		return;
	}

	/*【修复】原来这里是 while(WeiBa_Bit) 无限阻塞，摇尾巴期间表情任务会被饿死。
	  现在每次只走 20 小步（约 120ms）就返回，让调度器还能跑其它任务。*/
	for(n=0;n<20;n++)
	{
		WeiBa_Value=(uint16_t)((int16_t)WeiBa_Value + ((int16_t)WeiBa_Dir * 2));

		if(WeiBa_Value>=150) { WeiBa_Dir=-1; }
		if(WeiBa_Value<=30)  { WeiBa_Dir=1;  }

		WServo_Angle(WeiBa_Value);
		Delay_ms(6);
	}
}

/*==============================================================================
 * 跳跃
 *============================================================================*/

void Action_JumpU(void)//向前跳
{
	if(TiaoTurn==0)
	{
		Servo_Angle2(140);
		Servo_Angle3(35);
		Delay_ms(SpeedDelay);

		Servo_Angle1(140);
		Servo_Angle4(35);
		Delay_ms(SpeedDelay+80);

		TiaoTurn=1;
	}
	else
	{
		Servo_Angle3(140);
		Servo_Angle2(35);
		Delay_ms(SpeedDelay);

		Servo_Angle4(140);
		Servo_Angle1(35);
		Delay_ms(SpeedDelay+80);

		TiaoTurn=0;
	}

	Action_Mode=2;
}

void Action_JumpD(void)//向后跳
{
	if(TiaoTurn2==0)
	{
		Servo_Angle4(35);
		Servo_Angle1(140);
		Delay_ms(SpeedDelay);

		Servo_Angle3(35);
		Servo_Angle2(140);
		Delay_ms(SpeedDelay);

		Action_Mode=12;
		TiaoTurn2=1;
	}
	else
	{
		Servo_Angle3(35);
		Servo_Angle2(140);
		Delay_ms(SpeedDelay);

		Servo_Angle4(35);
		Servo_Angle1(140);
		Delay_ms(SpeedDelay);

		Action_Mode=12;
		TiaoTurn2=0;
	}
}

/*==============================================================================
 * 手势动作
 *============================================================================*/

void Action_Hello(void)//打招呼
{
	uint16_t HelloNumber=HelloRepeatnumber;
	int i;

	Servo_Angle3(20);
	Servo_Angle4(45);
	Delay_ms(80);
	Servo_Angle1(90);

	while(HelloNumber && (Action_Mode==13))
	{
		Face_Service();/*动作是阻塞的，每轮给表情一次刷新机会*/
		if(Action_Mode!=13)break;

		for(i=0;i<=45;i++)
		{
			if(Action_Mode!=13)break;
			Servo_Angle2((float)i);
			Delay_ms(SwingDelay);
		}
		for(i=45;i>0;i--)
		{
			if(Action_Mode!=13)break;
			Servo_Angle2((float)i);
			Delay_ms(SwingDelay);
		}

		if(Action_Mode!=13)break;

		if(HelloNumber) { HelloNumber--; }
	}

	Legs_All(90);

	if(Action_Mode==13)
	{
		Action_Mode=2;
	}
}

void Action_stretch(void)//伸懒腰
{
	int i;

	Servo_Angle3(90);
	Servo_Angle4(90);
	Delay_ms(80);

	for(i=90;i>10;i--)
	{
		Servo_Angle1((float)i);
		Servo_Angle2((float)i);
		if(Action_Mode!=14)break;
		Delay_ms(15);
		Face_Service();
	}
	for(i=10;i<90;i++)
	{
		Servo_Angle1((float)i);
		Servo_Angle2((float)i);
		if(Action_Mode!=14)break;
		Delay_ms(15);
		Face_Service();
	}
	for(i=90;i<170;i++)
	{
		Servo_Angle3((float)i);
		Servo_Angle4((float)i);
		if(Action_Mode!=14)break;
		Delay_ms(15);
		Face_Service();
	}
	for(i=170;i>90;i--)
	{
		Servo_Angle3((float)i);
		Servo_Angle4((float)i);
		if(Action_Mode!=14)break;
		Delay_ms(15);
		Face_Service();
	}

	Legs_All(90);

	if(Action_Mode==14)
	{
		Action_Mode=15;
	}
}

void Action_Lstretch(void)//后腿拉伸
{
	int i;
	int RepeatTime=3;

	Face_Service();/*动作是阻塞的，进来先把新表情画出来*/

	/*---- 左后腿 ----*/
	Servo_Angle1(90);
	Servo_Angle2(20);
	Delay_ms(60);
	Servo_Angle4(110);

	for(i=90;i<180;i++)
	{
		if(Action_Mode!=15)break;
		Servo_Angle3((float)i);
		Delay_ms(6);
	}
	while(RepeatTime && (Action_Mode==15))
	{
		for(i=180;i>150;i--)
		{
			if(Action_Mode!=15)break;
			Servo_Angle3((float)i);
			Delay_ms(15);
		}
		RepeatTime--;
	}
	if(Action_Mode!=15){ Legs_All(90); return; }

	Delay_ms(100);
	Servo_Angle1(90);
	Servo_Angle2(90);
	if(Action_Mode!=15){ Legs_All(90); return; }
	Delay_ms(80);
	Servo_Angle3(90);
	Servo_Angle4(90);
	Delay_ms(100);
	if(Action_Mode!=15){ Legs_All(90); return; }

	RepeatTime=3;

	/*---- 右后腿 ----*/
	Servo_Angle2(90);
	Servo_Angle1(20);
	if(Action_Mode!=15){ Legs_All(90); return; }
	Delay_ms(60);
	Servo_Angle3(110);

	for(i=90;i<180;i++)
	{
		if(Action_Mode!=15)break;
		Servo_Angle4((float)i);
		Delay_ms(6);
	}
	while(RepeatTime && (Action_Mode==15))
	{
		for(i=180;i>150;i--)
		{
			if(Action_Mode!=15)break;
			Servo_Angle4((float)i);
			Delay_ms(15);
		}
		RepeatTime--;
	}

	Legs_All(90);

	if(Action_Mode==15)
	{
		Action_Mode=2;
	}
}

/*==============================================================================
 * 动作判断
 *============================================================================*/

/**
  * @brief  判断是不是一个合法的命令字节
  * @note   【修复】蓝牙链路在配对/断开/干扰时会产生随机字节。原始实现里任何字节
  *         都会把 Sustainedmove 置 1，再叠加 PAnumbers 的 uint16_t 下溢，
  *         一个乱码字节就能让机器人连续走十几到几十小时。
  *         这里先做白名单校验，非法字节直接丢弃。
  * @retval 1 合法；0 非法
  */
static uint8_t Command_IsValid(uint16_t cmd)
{
	switch(cmd)
	{
		case 0x29: case 0x30: case 0x31: case 0x32: case 0x33:
		case 0x34: case 0x35: case 0x36: case 0x37: case 0x38:
		case 0x39: case 0x40: case 0x41: case 0x42: case 0x43:
		case 0x44: case 0x45: case 0x46: case 0x47: case 0x48:
		case 0x49: case 0x50: case 0x51:
		case 0x52: case 0x53: case 0x54: case 0x55:
			return 1;
		default:
			return 0;
	}
}

/*动作判断*/
//Serial_type:串口通信类型：0为语音，1为蓝牙
void PetAction_Judgmengt(uint8_t Serial_type)
{
	if(Serial_type==0)
	{
		/*非法字节直接丢弃，避免干扰把正在执行的动作打断*/
		if(!Command_IsValid(RxData_Voice)) { return; }

		Sustainedmove=0;//持续运动关闭（语音是"单次执行"）
		switch(RxData_Voice)
		{
			case 0x29://放松的趴下
				Face_Mode=0;
				Action_Mode=0;
				break;
			case 0x30://蹲下
				Face_Mode=1;
				Action_Mode=1;
				break;
			case 0x31://直立
				Face_Mode=5;
				Action_Mode=2;
				break;
			case 0x32://趴下
				Face_Mode=1;
				Action_Mode=3;
				break;
			case 0x33://前进
				Face_Mode=2;
				Action_Mode=4;
				break;
			case 0x34://后退
				Face_Mode=2;
				Action_Mode=5;
				break;
			case 0x35://左转
				Face_Mode=2;
				Action_Mode=6;
				break;
			case 0x36://右转
				Face_Mode=2;
				Action_Mode=7;
				break;
			case 0x37://摇摆
				Face_Mode=4;
				SwingSeconds=0;   /*清掉 0x52~0x55 残留的时长，恢复固定次数*/
				Action_Mode=8;
				break;
			case 0x38://减少移动延迟，增加移动速度（300~550ms 循环，步进 50ms）
				if(SpeedDelay==350U){Face_Mode=3;}   /*已经是最快档，表情提示*/
				if(SpeedDelay>300U)
				SpeedDelay-=50U;
				else{Face_Mode=2;SpeedDelay=550U;}
				break;
			case 0x39://减少摇摆延迟，增加摇摆速度
				if(SwingDelay==4){Face_Mode=3;}
				if(SwingDelay>3)
				SwingDelay--;
				else{Face_Mode=4;SwingDelay=9;}
				break;                      /*【修复】原来漏了 break，会贯穿到 0x40*/
			case 0x40://摇尾巴
				WeiBa_Bit^=1;
				Face_Mode=1;
				break;
			case 0x41://向前跳
				Face_Mode=2;
				Action_Mode=10;
				break;
			case 0x42://向后跳
				Face_Mode=2;
				Action_Mode=11;
				break;
			case 0x43://打招呼
				Face_Mode=6;
				Action_Mode=13;
				break;
			case 0x44://开启灯光
				AllLed=1;
				break;
			case 0x45://关闭灯光
				AllLed=0;
				break;
			case 0x46://开启呼吸灯
				BreatheLed=1;
				break;
			case 0x47://关闭呼吸灯
				BreatheLed=0;
				break;
			case 0x48://伸懒腰
				Face_Mode=6;
				Action_Mode=14;
				break;
			case 0x49://拉伸
				Face_Mode=6;
				Action_Mode=15;
				break;
			case 0x50://显示电量
				Battery_Bit^=1;
				break;
			case 0x51://显示天气（语音通道）
				/*【先同步再显示】桌宠自己不联网，天气只能由手机去取。
				   这里先通过蓝牙给手机发一个"请求同步"字节，手机收到后
				   自动定位 -> 请求接口 -> 把天气帧发回来。
				   Face_Config 是 30ms 周期的任务，帧一到下一个周期就会刷新，
				   所以这里不用阻塞等待（等蓝牙会让整个调度停摆）。
				   手机没连着的话，就停留在上一次的数据或 NO DATA。*/
				BlueTooth_SendByte(WEATHER_REQ_SYNC);
				Face_Mode=7;
				Action_Mode=2;   /*站起来，否则动作阻塞时天气刷不出来*/
				break;
			case 0x52://摇摆 26 秒
				Face_Mode=4;
				SwingSeconds=26;
				Action_Mode=8;
				break;
			case 0x53://摇摆 27 秒
				Face_Mode=4;
				SwingSeconds=27;
				Action_Mode=8;
				break;
			case 0x54://摇摆 16 秒
				Face_Mode=4;
				SwingSeconds=16;
				Action_Mode=8;
				break;
			case 0x55://摇摆 15 秒
				Face_Mode=4;
				SwingSeconds=15;
				Action_Mode=8;
				break;
			default:
				break;
		}
	}
	else
	{
		/*【修复】非法字节直接丢弃：这一步是防止"蓝牙一连接就乱动"的关键*/
		if(!Command_IsValid(RxData_Blue)) { return; }

		/*【修复】0x52~0x55 只走语音通道，蓝牙不收。
		   这几个码对应 ASCII 的 'R' 'S' 'T' 'U'，而蓝牙模块在手机
		   连接/断开时会往串口吐 "CONNECTED" / "DISCONNECTED" 这类状态串，
		   里面的 'S' 'T' 正好被当成"摇摆 27/16 秒"执行 —— 表现就是
		   一连接（或一断开）桌宠自己摇起来。App 里也没有这四个按钮。*/
		if((RxData_Blue>=0x52U) && (RxData_Blue<=0x55U)) { return; }

		Sustainedmove=1;//持续运动开启（蓝牙是"一直走，直到下一条命令"）
		switch(RxData_Blue)
		{
			case 0x29://放松的趴下
				Face_Mode=0;
				Action_Mode=0;
				break;
			case 0x30://蹲下
				Face_Mode=1;
				Action_Mode=1;
				break;
			case 0x31://直立
				Face_Mode=5;
				Action_Mode=2;
				break;
			case 0x32://趴下
				Face_Mode=1;
				Action_Mode=3;
				break;
			case 0x33://前进
				Face_Mode=2;
				Action_Mode=4;
				break;
			case 0x34://后退
				Face_Mode=2;
				Action_Mode=5;
				break;
			case 0x35://左转
				Face_Mode=2;
				Action_Mode=6;
				break;
			case 0x36://右转
				Face_Mode=2;
				Action_Mode=7;
				break;
			case 0x37://摇摆
				Face_Mode=4;
				SwingSeconds=0;   /*清掉 0x52~0x55 残留的时长，恢复固定次数*/
				Action_Mode=8;
				break;
			case 0x38://减少移动延迟，增加移动速度（300~550ms 循环，步进 50ms）
				if(SpeedDelay==350U){Face_Mode=3;}   /*已经是最快档，表情提示*/
				if(SpeedDelay>300U)
				SpeedDelay-=50U;
				else{Face_Mode=2;SpeedDelay=550U;}
				break;
			case 0x39://减少摇摆延迟，增加摇摆速度
				if(SwingDelay==4){Face_Mode=3;}
				if(SwingDelay>3)
				SwingDelay--;
				else{Face_Mode=4;SwingDelay=9;}
				break;                      /*【修复】原来漏了 break，会贯穿到 0x40*/
			case 0x40://摇尾巴
				WeiBa_Bit^=1;
				Face_Mode=1;
				break;
			case 0x41://向前跳
				Face_Mode=2;
				Action_Mode=10;
				break;
			case 0x42://向后跳
				Face_Mode=2;
				Action_Mode=11;
				break;
			case 0x43://打招呼
				Face_Mode=6;
				Action_Mode=13;
				break;
			case 0x44://开启灯光
				AllLed=1;
				break;
			case 0x45://关闭灯光
				AllLed=0;
				break;
			case 0x46://开启呼吸灯
				BreatheLed=1;
				break;
			case 0x47://关闭呼吸灯
				BreatheLed=0;
				break;
			case 0x48://伸懒腰
				Face_Mode=6;
				Action_Mode=14;
				break;
			case 0x49://拉伸
				Face_Mode=6;
				Action_Mode=15;
				break;
			case 0x50://显示电量
				Battery_Bit^=1;
				break;
			case 0x51://显示天气（蓝牙通道）
				Sustainedmove=0; /*必须关掉持续运动，否则正在走的动作会把屏幕刷新卡住*/
				Face_Mode=7;
				Action_Mode=2;
				break;
			/*0x52~0x55 是语音专用的定时摇摆，蓝牙通道已在上面的分支里挡掉*/
			default:
				break;
		}
	}
}

/*执行动作*/
void PetAction_Perform(void)
{
		uint16_t ModeBefore=Action_Mode;   /*这次进来时是什么动作*/

		/*【关键】进动作之前先把新表情画出来。
		   动作函数一进去就阻塞几十毫秒到几十秒，30ms 的表情任务
		   在此期间没有机会执行，屏幕会一直停在上一个表情
		   （天气表情"切不走"就是这么来的）。*/
		Face_Service();

		if(Action_Mode==0){Action_relaxed_getdowm();}//放松趴下
		else if(Action_Mode==1){Action_sit();}//坐下
		else if(Action_Mode==2){Action_upright();}//站立
		else if(Action_Mode==3){Action_getdowm();}//趴下
		else if(Action_Mode==4){Action_advance();}//前进
		else if(Action_Mode==5){Action_back();}//后退
		else if(Action_Mode==6){Action_Lrotation();}//左转
		else if(Action_Mode==7){Action_Rrotation();}//右转
		else if(Action_Mode==8){Action_Swing();}//摇摆
		else if(Action_Mode==9){Action_SwingTail();}//摇尾巴
		else if(Action_Mode==10){Action_JumpU();}//前跳
		else if(Action_Mode==11){Action_JumpD();}//后跳
		else if(Action_Mode==12){Action_upright2();}//站立方式2
		else if(Action_Mode==13){Action_Hello();}//打招呼
		else if(Action_Mode==14){Action_stretch();}//伸懒腰
		else if(Action_Mode==15){Action_Lstretch();}//后腿拉伸

		/*【动作结束后给语音留时间】
		  各动作函数收尾时都会把 Action_Mode 改成 9（待机摇尾巴），
		  所以"进来时不是 9、出去时变成 9"就代表这一串动作做完了。
		  这时停 2 秒再让调度继续，否则动作一结束马上干下一件事，
		  语音还没说完就被打断。
		  只在语音通道生效（Sustainedmove==0）：蓝牙是连续动作，
		  中间插 2 秒会一顿一顿的。*/
		if((ModeBefore!=9U) && (Action_Mode==9U) && (Sustainedmove==0U))
		{
			Delay_ms(ACTION_TAIL_DELAY_MS);
		}
}
