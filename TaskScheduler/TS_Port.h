/**
  ******************************************************************************
  * @file    TS_Port.h
  * @brief   TaskScheduler 的 STM32F103 移植层（时基 + 让步）
  *
  * 本文件是纯 C 接口，.c 和 .cpp 都能直接包含。
  *
  * 时基来源：TIM4
  *   - TIM4 在本工程中是空闲的（TIM2 被 PWM.c/Servo.c 占用，
  *     TIM3 被 Timer.c 占用并同时承担 50Hz 舵机 PWM，SysTick 被 Delay.c 反复重配置）
  *   - 配置为 72MHz / 72 = 1MHz 计数，重装载 1000，即 1ms 一次更新中断
  *   - 中断里把软件计数器加一，得到 millis()
  *   - 微秒值 = 软件计数器 * 1000 + TIM4->CNT（TIM4 计数频率就是 1MHz，
  *     所以 CNT 的数值天然就是“当前这一毫秒内已经过去的微秒数”）
  *
  * 与库的对应关系（见 TaskScheduler.h 中 _TASK_NON_ARDUINO 分支）：
  *   _task_millis() -> TS_Millis()
  *   _task_micros() -> TS_Micros()   （仅在开启 _TASK_TIMECRITICAL 等选项时需要）
  *   _task_yield()  -> TS_Yield()
  ******************************************************************************
  */

#ifndef __TS_PORT_H
#define __TS_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  初始化移植层时基（配置并使能 TIM4 的 1ms 更新中断）。
  * @note   必须在 main() 里、进入调度循环之前调用一次。
  *         本函数不修改 TIM2/TIM3/SysTick，也不影响 Delay.c 的用法。
  * @param  无
  * @retval 无
  */
void TS_Port_Init(void);

/**
  * @brief  读取系统运行时间，单位毫秒（对应 Arduino 的 millis()）。
  * @note   由 TIM4 中断累加，约 49.7 天回绕（0xFFFFFFFF ms）。
  *         调度器内部使用“无符号差值比较”，回绕是安全的。
  * @param  无
  * @retval 自 TS_Port_Init() 起经过的毫秒数
  */
uint32_t TS_Millis(void);

/**
  * @brief  读取系统运行时间，单位微秒（对应 Arduino 的 micros()）。
  * @note   分辨率 1us，约 71.6 分钟回绕（0xFFFFFFFF us）。
  *         只适合测量短时间差，不要用来做长时间累计。
  * @param  无
  * @retval 自 TS_Port_Init() 起经过的微秒数
  */
uint32_t TS_Micros(void);

/**
  * @brief  任务间的“让步”点（对应 Arduino 的 yield()）。
  * @note   裸机、协作式、单线程环境下没有别的线程需要被让出，
  *         因此这里是空操作（一条 __NOP），只作为将来接入
  *         RTOS / 低功耗 __WFI() 的预留挂钩。
  * @param  无
  * @retval 无
  */
void TS_Yield(void);

#ifdef __cplusplus
}
#endif

#endif /* __TS_PORT_H */
