/**
  ******************************************************************************
  * @file    TS_TaskScheduler.cpp
  * @brief   TaskScheduler 库在本工程中的【唯一实现编译单元】
  *
  * 为什么要单独一个 .cpp 来编译库的实现？
  *   上游库在 _TASK_NON_ARDUINO 模式下，会把时基钩子声明成
  *       static unsigned long _task_millis();
  *   也就是【内部链接】的函数 —— 每个包含了 TaskScheduler.h 的编译单元
  *   都必须自己提供一份定义，否则链接时报 undefined symbol。
  *
  *   因此本移植的做法是：
  *       业务代码  -> 只包含 TaskSchedulerDeclarations.h（纯声明）
  *       本文件    -> 唯一包含 TaskScheduler.h（含实现）的编译单元，
  *                    并在这里把时基钩子接到 TS_Port.c 上
  *   这样既保持了上游源码的实现逻辑不变（仅有两处 C++11→C++03 语法回退，
  *   见 TaskSchedulerDeclarations.h 里的 [TS-PORT] 注释与 README_PORT.md 第 5 节），
  *   又支持多文件工程。
  *
  * 对应的上游用法可参考 examples/Scheduler_example29_NonArduino
  * 以及库的 _TASK_HEADER_AND_CPP 选项说明。
  ******************************************************************************
  */

/* 1) 编译期选项：必须在库头文件之前 */
#include "TS_Config.h"

/* 2) 移植层（TIM4 1ms 时基） */
#include "TS_Port.h"

/* 3) 库实现：本工程中只有这一个文件包含它 */
#include "TaskScheduler.h"

/*==============================================================================
 * 把库需要的三个时基钩子接到 STM32 移植层
 *
 * 钩子的声明条件与 TaskScheduler.h 里的分支严格对应，见该文件
 * “------------------ TaskScheduler implementation --------------------” 一节。
 *============================================================================*/

#if defined(_TASK_MICRO_RES)

/* 微秒分辨率模式：库只用 _task_micros() */
static unsigned long _task_micros(void)
{
	return (unsigned long)TS_Micros();
}

#else

/* 默认毫秒分辨率：库只用 _task_millis() */
static unsigned long _task_millis(void)
{
	return (unsigned long)TS_Millis();
}

/* micros() 仅在需要测量调度时序/空闲休眠时才被库引用 */
#if defined(_TASK_TIMECRITICAL) || defined(_TASK_SLEEP_ON_IDLE_RUN)
static unsigned long _task_micros(void)
{
	return (unsigned long)TS_Micros();
}
#endif /* _TASK_TIMECRITICAL || _TASK_SLEEP_ON_IDLE_RUN */

#endif /* _TASK_MICRO_RES */

#if !defined(_TASK_DO_NOT_YIELD)
static void _task_yield(void)
{
	TS_Yield();
}
#endif /* _TASK_DO_NOT_YIELD */
