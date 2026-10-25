/**
  ******************************************************************************
  * @file    TS_TaskScheduler.h
  * @brief   TaskScheduler 业务侧统一入口（C++ 头文件）
  *
  * 用法（只能被 .cpp 文件包含）：
  *
  *     #include "TS_TaskScheduler.h"
  *
  *     Scheduler ts;
  *     void myCallback() { ... }
  *     Task myTask(100 * TASK_MILLISECOND, TASK_FOREVER, &myCallback, &ts, true);
  *
  * 说明：
  *   - 本头文件只暴露【声明】，库的【实现】全部编译在 TS_TaskScheduler.cpp 里，
  *     所以整个工程可以有任意多个 .cpp 引用调度器而不会出现重复定义。
  *   - 顺序很重要：必须先包含 TS_Config.h，再包含库头文件。
  *     本文件已经保证了这个顺序，请不要绕过它直接包含库的原始头文件
  *     （除非你是在 TS_TaskScheduler.cpp 这类“库实现”文件里）。
  *   - .c 文件（C 语言）不能包含本文件；C 代码请改用 User/SchedulerApp.h
  *     暴露的 C 接口。
  ******************************************************************************
  */

#ifndef __TS_TASKSCHEDULER_H
#define __TS_TASKSCHEDULER_H

#ifdef __cplusplus

/* 1) 编译期选项：必须最先出现 */
#include "TS_Config.h"

/* 2) 移植层时基接口 */
#include "TS_Port.h"

/* 3) 库的类声明（Task / Scheduler / StatusRequest ...） */
#include "TaskSchedulerDeclarations.h"

#endif /* __cplusplus */

#endif /* __TS_TASKSCHEDULER_H */
