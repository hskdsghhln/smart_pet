/**
  ******************************************************************************
  * @file    TS_Config.h
  * @brief   TaskScheduler 编译期配置（STM32F103 + 标准外设库 + Keil MDK/AC5 移植）
  *
  * 说明：
  *   1. 这是整个库里唯一需要按项目修改的配置文件，是编译期选项的“单一事实来源”。
  *   2. 本文件必须在 TaskSchedulerDeclarations.h / TaskScheduler.h 之前被包含，
  *      否则选项不会生效。请统一通过 "TS_TaskScheduler.h"（业务代码）
  *      或 "TaskScheduler.h"（TS_TaskScheduler.cpp）间接包含本文件。
  *   3. 每个编译单元看到的选项必须完全一致，否则 Task/Scheduler 类的内存布局
  *      会在不同 .c/.cpp 之间不一致，导致难以排查的运行时错误。
  *
  * 参考：https://github.com/arkhipenko/TaskScheduler
  *       TaskSchedulerDeclarations.h 顶部的 CompileTimeOptions 文档
  ******************************************************************************
  */

#ifndef __TS_CONFIG_H
#define __TS_CONFIG_H

/*==============================================================================
 * 一、必选项
 *============================================================================*/

/**
  * @brief 非 Arduino 环境编译。
  *        置位后库不再包含 <Arduino.h>，也【不会】再自动声明
  *        _task_millis() / _task_micros() / _task_yield()，
  *        改由移植层（TaskScheduler/TS_TaskScheduler.cpp）提供实现。
  */
#define _TASK_NON_ARDUINO

/*==============================================================================
 * 二、按需开启的功能（本项目全部开启，均为编译期裁剪，不用的功能不占 Flash/RAM）
 *============================================================================*/

/**
  * @brief 状态请求对象（StatusRequest）。
  *        允许任务之间用事件/握手的方式互相触发，而不只是靠时间。
  *        本项目里语音(USART1)/蓝牙(USART3)中断改变 Action_Mode、Face_Mode 后，
  *        可以用它把“事件”通知给调度器里的任务。
  */
#define _TASK_STATUS_REQUEST

/**
  * @brief 任务本地存储指针（Local Task Storage, void*）。
  *        让同一份回调代码可以被多个 Task 实例复用（回调里用 getLtsPointer() 区分）。
  */
#define _TASK_LTS_POINTER

/**
  * @brief 任务 ID 与看门狗控制点（Control Point）。
  *        每个 Task 自动获得一个递增 ID，回调内可用 setControlPoint()/getControlPoint()
  *        标记执行进度，便于错误定位与看门狗喂狗判断。
  */
#define _TASK_WDT_IDS

/**
  * @brief 调度时序监控：getStartDelay() / getOverrun() / getCpuLoad() 等。
  *        会额外调用 _task_micros()，用于统计“任务实际运行时刻比计划晚了多少微秒”
  *        以及调度器本身的 CPU 占用率。移植层已提供微秒级实现（见 TS_Port.c）。
  */
#define _TASK_TIMECRITICAL

/**
  * @brief 单个任务的整体超时（Task Timeout）。
  *        可给任务设置 setTimeout()，超时后任务被禁用并调用 OnDisable，
  *        相当于“单个任务的看门狗”。本项目舵机动作是阻塞式的，
  *        给动作任务加超时能防止动作卡死导致系统失去响应。
  */
#define _TASK_TIMEOUT

/**
  * @brief 多种调度策略：TASK_SCHEDULE / TASK_SCHEDULE_NC / TASK_INTERVAL，
  *        以及 setIntervalNodelay() 的 KEEP / RECALC / RESET 选项。
  *        TASK_INTERVAL（总是从“本次实际执行时刻”起算下一个周期）
  *        对阻塞式任务更友好，能避免任务排队追赶。
  */
#define _TASK_SCHEDULING_OPTIONS

/**
  * @brief 暴露任务链：getNextTask() / getPreviousTask() / getFirstTask() / getLastTask()。
  *        便于遍历、调试、统计整个任务链。
  */
#define _TASK_EXPOSE_CHAIN

/*==============================================================================
 * 三、明确不开启的功能（连同原因一起记录，避免后来者误开）
 *============================================================================*/

/* #define _TASK_SLEEP_ON_IDLE_RUN  */  /* 空闲时进 SLEEP_IDLE：仅 AVR/Teensy 有对应省电汇编，
                                              Cortex-M3 上应改用 __WFI()，且不能用库自带的实现       */

/* #define _TASK_PRIORITY           */  /* 分层优先级：本项目任务数量少，不需要多层调度器；
                                             且该模式会限制 execute() 的重入行为                   */

/* #define _TASK_MICRO_RES          */  /* 微秒分辨率：把所有时间参数的单位变成微秒。
                                             本项目不使用，保留默认的毫秒分辨率                     */

/* #define _TASK_STD_FUNCTION       */  /* std::function 回调：依赖 libstdc++，裸机 Keil 工程不引入 */

/* #define _TASK_OO_CALLBACKS       */  /* 继承式回调：会改变 Task 构造函数签名，
                                             本项目使用 C 风格函数指针回调                          */

/* #define _TASK_SELF_DESTRUCT      */  /* 任务自毁（disable 后 delete 自己）：
                                             任务都是静态/全局对象，delete 静态对象会直接跑飞        */

/* #define _TASK_TICKLESS           */  /* FreeRTOS 无滴答休眠：本项目为裸机，无 RTOS            */

/* #define _TASK_DO_NOT_YIELD       */  /* 关闭 execute() 里的 yield()：保留 yield 更灵活，
                                             移植层里它是一条 __NOP()，代价为 0                     */

/* #define _TASK_ISR_SUPPORT        */  /* ESP 专属：把控制方法放进 IRAM，STM32 无此概念         */

/* #define _TASK_DEBUG              */  /* 把所有成员改成 public 便于调试：会破坏封装，发布版不开 */

/* #define _TASK_INLINE             */  /* 所有方法 inline：本移植采用“声明/实现分离”的编译方式
                                             （见 TS_TaskScheduler.cpp），不需要 inline 模式        */

/* #define _TASK_HEADER_AND_CPP     */  /* 上游 TaskScheduler.cpp 的空壳开关，本移植用法不同，
                                             详见 README_PORT.md                                   */

/* #define _TASK_THREAD_SAFE        */  /* 线程安全请求队列：需要 __attribute__((weak)) 与互斥量，
                                             属于 ARMCC 不支持的语法，且裸机单线程用不到           */

#endif /* __TS_CONFIG_H */
