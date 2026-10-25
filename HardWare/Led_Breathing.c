#include "stm32f10x.h"                  // Device header
#include "Variable.h"
#include "PWM.h"

/*注意LED的GPIO与PWM配置已经在PWM.c中配置好了，这个函数是在中断中执行的函数*/

/*==================================================================================
 * 调用周期与呼吸节奏
 *
 * 本函数由 TIM3 更新中断驱动。TIM3 的 TIM_Period=20000 同时决定了舵机 PWM 必须
 * 是 50Hz，所以不能通过改 TIM3 周期来调整本函数的调用频率，改为在 main.c 的
 * TIM3_IRQHandler 里做 2 分频：20ms x 2 = 40ms 调用一次。
 *
 * 调用周期翻倍后，若步长不变，呼吸节奏自然会慢一倍（4s -> 8s）。
 * 本工程按需求把呼吸灯调慢，因此这里直接给出目标节奏，步长由它推算：
 *
 *     8s 渐亮  +  8s 渐暗  +  800ms 停顿
 *
 * 想改快/改慢只需要动 LED_BREATH_FADE_MS 和 LED_BREATH_HOLD_MS 两个数，
 * 步长会自动跟着变（前提是它们能被 LED_BREATH_PERIOD_MS 整除，下面有编译期检查）。
 * 想恢复移植前的 4s 节奏，把两个值改成 4000U 和 400U 即可。
 *================================================================================*/
#define LED_BREATH_PERIOD_MS    40U         /* 本函数的实际调用周期(ms)，与 main.c 的分频保持一致 */
#define LED_BREATH_MAX          20000U      /* PWM 比较值上限(拉满亮度) */
#define LED_BREATH_FADE_MS      8000U       /* 从全暗到全亮(或反之)所需时间(ms) */
#define LED_BREATH_HOLD_MS      800U        /* 全暗后的停顿时间(ms) */

/* 步长必须整除，否则亮度台阶不均匀，且下面的溢出保护会提前收尾 */
#if (LED_BREATH_FADE_MS % LED_BREATH_PERIOD_MS) != 0
#error "LED_BREATH_FADE_MS 必须能被 LED_BREATH_PERIOD_MS 整除"
#endif
#if (LED_BREATH_HOLD_MS % LED_BREATH_PERIOD_MS) != 0
#error "LED_BREATH_HOLD_MS 必须能被 LED_BREATH_PERIOD_MS 整除"
#endif

#define LED_BREATH_STEP         (LED_BREATH_MAX / (LED_BREATH_FADE_MS / LED_BREATH_PERIOD_MS))
#define LED_BREATH_WAIT_STEP    (LED_BREATH_MAX / (LED_BREATH_HOLD_MS / LED_BREATH_PERIOD_MS))

void LED_Breathing(void)
{
		if(AllLed==1 && BreatheLed==0)//如果灯光开启且不开启呼吸灯,亮度拉满
		{
			PWM_LED1(LED_BREATH_MAX);
			PWM_LED2(LED_BREATH_MAX);
		}
		else if(AllLed==1 && BreatheLed==1)//如果灯光开启且开启呼吸灯
		{
			if(PanDuan==1)
			{
				/* 渐亮：先判断再加，防止 HuXi 越过上限后回绕 */
				if(HuXi <= (LED_BREATH_MAX - LED_BREATH_STEP))
				{
					HuXi+=LED_BREATH_STEP;
				}
				else
				{
					HuXi=LED_BREATH_MAX;
				}

				PWM_LED1(HuXi);
				PWM_LED2(HuXi);

				if(HuXi>=LED_BREATH_MAX){HuXi=LED_BREATH_MAX;PanDuan=2;}
			}
			else if(PanDuan==2)
			{
				/* 渐暗：先判断再减，防止 HuXi 减到负数后回绕成很大值 */
				if(HuXi >= LED_BREATH_STEP)
				{
					HuXi-=LED_BREATH_STEP;
				}
				else
				{
					HuXi=0;
				}

				PWM_LED1(HuXi);
				PWM_LED2(HuXi);

				if(HuXi==0){PanDuan=3;}
			}
			else if(PanDuan==3)
			{
				Wait+=LED_BREATH_WAIT_STEP;
				if(Wait>=LED_BREATH_MAX)
				{
					PanDuan=1;
					Wait=0;
				}
			}
		}
		else if(AllLed==0)
		{
			PWM_LED1(0);
			PWM_LED2(0);
		}
}
