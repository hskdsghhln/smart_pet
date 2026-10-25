#include "stm32f10x.h"                  // Device header
#include "AD.h"
#include "Variable.h"


/*==============================================================================
 * 电量滤波参数
 *
 * 原实现是"每 20ms 采一个样本，攒满 100 个求平均"，即 2 秒的滑动窗口。
 * 本工程移植 TaskScheduler 后，TIM3 中断做了 2 分频，采样周期变为 40ms，
 * 同样 100 个样本的窗口因此变成 4 秒（电量数字刷新变慢）。
 *
 * 想恢复原来的 2 秒窗口，把 BATTERY_FILTER_SAMPLES 改成 50 即可，
 * 下面的比较与求平均都用这个宏，改一处就够。
 *
 * 注意：Battery_num 是 uint8_t，本宏不要超过 254。
 *============================================================================*/
#define BATTERY_FILTER_SAMPLES      100U


/*初始化ADC函数*/

void GetBattery_Init(void)
{
	AD_Init();
}

/*获取一次电量(平均前)*/
uint16_t GetBattery(void)
{
	Battery_Value=(3.3*4*Get_ADC()/4096)*100-300;
	return (uint16_t)Battery_Value;
}

/*获取平均电量(读取 BATTERY_FILTER_SAMPLES 次 adc 对应的电量值，再取平均数)
 *
 * 【移植时顺带修掉的原有 bug】
 * 这里的累加变量 temp1 原来声明为 uint16_t，而单个样本是
 *     (3.3*4*ADC/4096)*100-300
 * 满量程时约 1020，累加 100 个样本最大可达 102000，远超 uint16_t 的 65535。
 *
 * 正常电池电压下单个样本只有 0~120 左右（Face_Config.c 就是按这个范围显示的），
 * 累加 100 个也才 12000，不会溢出 —— 所以这个 bug 平时看不出来。
 * 但一旦 ADC 读到异常偏高的值（电池未接、分压电阻虚焊、输入悬空），
 * 单个样本超过 655 就会让 temp1 回绕，CurBattery 随即显示成错误数字。
 * 实测阈值：ADC > 2963 时开始回绕。
 *
 * 现在 temp1 已改为 uint32_t（见 Variable.h / Variable.c），
 * 累加与求平均都是安全的，正常区间内的显示结果与修改前完全一致。
 */
void GetCur_Power(void)
{
		if(++Battery_num<=BATTERY_FILTER_SAMPLES)
		{
			temp=GetBattery();
			temp1+=temp;
		}
		else
		{
			CurBattery=(uint16_t)(temp1/BATTERY_FILTER_SAMPLES);
			temp1=0;
			Battery_num=0;
		}
}
