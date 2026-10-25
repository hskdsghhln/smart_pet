/**
  ******************************************************************************
  * @file    SchedulerApp.h
  * @brief   TaskScheduler 应用层对外接口（纯 C，可被 .c 文件直接包含）
  *
  * 为什么要包一层 C 接口？
  *   TaskScheduler 是 C++ 库，而本工程的主体（main.c 以及各硬件驱动）都是 C。
  *   armcc 按扩展名区分语言：.c 走 C 编译器，.cpp 走 C++ 编译器。
  *   C 编译单元无法直接使用 C++ 的 Scheduler/Task 类，因此把调度器封装在
  *   User/SchedulerApp.cpp 里，只对外暴露下面两个 C 函数。
  *
  * 使用方式（User/main.c）：
  *     初始化阶段最后调用  SchedulerApp_Init();
  *     主循环里调用        SchedulerApp_Run();
  ******************************************************************************
  */

#ifndef __SCHEDULER_APP_H
#define __SCHEDULER_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  初始化任务调度：启动 1ms 时基(TIM4) 并注册所有任务。
  * @note   会配置 TIM4 的中断优先级(抢占 3)，因此必须在
  *         NVIC_PriorityGroupConfig() 之后调用。
  *         本工程中 BlueTooth_Init()/Voice_Init() 会设置优先级分组，
  *         所以请把本函数放在初始化序列的最后。
  * @param  无
  * @retval 无
  */
void SchedulerApp_Init(void);

/**
  * @brief  执行一次调度（内部就是 Scheduler::execute()）。
  * @note   这是一个"协作式"调度点：它会把本轮到期的任务依次调用完再返回，
  *         不会抢占任何正在运行的代码。因此必须被主循环反复调用，
  *         且不能有别的长时间阻塞代码插在它前面。
  * @param  无
  * @retval 无
  */
void SchedulerApp_Run(void);

#ifdef __cplusplus
}
#endif

#endif /* __SCHEDULER_APP_H */
