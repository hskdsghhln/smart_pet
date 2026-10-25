/**
  ******************************************************************************
  * @file    SchedulerApp.cpp
  * @brief   TaskScheduler 应用层：把原来 main.c 主循环里的工作改造成"任务"
  *
  * 原来的结构：
  *     while (1) {
  *         Face_Config();        // 刷 OLED 表情 + 电量
  *         PetAction_Perform();  // 执行舵机动作（内部是阻塞式的）
  *     }
  *     TIM3_IRQHandler() { GetCur_Power(); LED_Breathing(); }  // 20ms 硬实时
  *
  * 现在的结构（协作式任务链）：
  *     tFace   : 30ms 周期任务，负责 OLED 刷新
  *     tAction : 每个调度 pass 执行一次，负责舵机动作
  *     TIM3 中断里的电量检测/呼吸灯【保持不动】—— 原因见下面的"重要取舍"
  *
  * ---------------------------------------------------------------------------
  * 重要取舍：为什么电量检测与呼吸灯没有搬到任务里？
  * ---------------------------------------------------------------------------
  *   TaskScheduler 是【协作式】调度器，它只能在"任务主动返回"的位置切换任务，
  *   不能抢占正在执行的代码。而 PetAction_Perform() 内部是阻塞式实现
  *   （Action_advance / Action_Swing / Action_SwingTail 等会连续 Delay_ms 几十
  *   毫秒甚至无限循环，靠串口中断改 Action_Mode 来打断）。
  *
  *   一旦动作开始阻塞，任务链上排在后面的任务在动作结束前都不会有机会执行。
  *   呼吸灯的亮度正是靠"每 20ms 调用一次、每次把 PWM 比较值加减 100"累积出来的，
  *   一旦被饿死就会卡住不动；电量检测同理。
  *
  *   所以这两项属于"硬实时周期任务"，正确的位置就是定时器中断，
  *   而不是协作式调度器。等以后把 PetAction.c 重写成非阻塞状态机，
  *   它们才适合搬到任务链里。
  ******************************************************************************
  */

/* 1) 库的声明 + 编译期配置 + 移植层时基（只能被 C++ 编译单元包含） */
#include "TS_TaskScheduler.h"

/* 2)
 * 下面这些都是 C 头文件，必须用 extern "C" 包起来，
 * 否则 armcc 会按 C++ 规则对它们做名字修饰（name mangling），
 * 而它们的实现是在 .c 文件里按 C 规则编译的，链接时会找不到符号。
 */
extern "C" {
#include "stm32f10x.h"
#include "SchedulerApp.h"
#include "Face_Config.h"
#include "PetAction.h"
}

/*==============================================================================
 * 可调参数
 *============================================================================*/

/**
  * OLED 刷新周期（毫秒）。
  * 原来主循环是"每跑完一轮就刷一次"，实际节奏由 OLED_Update() 的全屏 I2C
  * 写入耗时决定（软件 I2C、1024 字节，量级 20~30ms）。
  * 这里取 30ms，刷新率约 33FPS，与原观感一致；
  * 想恢复到"每轮都刷"的老行为，把它改成 TASK_IMMEDIATE(0) 即可。
  */
#define TS_APP_FACE_PERIOD_MS       (30UL)

/*==============================================================================
 * 调度器与任务
 *
 * 注意：这些是全局对象，构造函数在 main() 之前（C++ 静态初始化）执行，
 *       会把自己挂到 ts 的任务链上。因此 ts 必须声明在任务之前。
 *       这里暂时不 enable，统一放到 SchedulerApp_Init() 里做，
 *       保证"时基已启动"和"任务已使能"的先后顺序一目了然。
 *============================================================================*/

/** 唯一的调度器实例 */
static Scheduler ts;

/**
  * @brief OLED 表情/电量刷新任务
  * @note  使用 TASK_INTERVAL 调度策略：
  *        下一个周期从"本次实际执行的时刻"起算，不做追赶。
  *        默认的 TASK_SCHEDULE 会记住原始时间点并在被阻塞后连续补跑，
  *        对刷屏这种"过期就过期"的任务毫无意义，只会白占 CPU。
  */
static Task tFace(TS_APP_FACE_PERIOD_MS * TASK_MILLISECOND,
                  TASK_FOREVER,
                  &Face_Config,
                  &ts,
                  false);

/**
  * @brief 舵机动作任务
  * @note  TASK_IMMEDIATE(0) 作为周期 => 每个调度 pass 都会被执行一次，
  *        与原来的 while(1) 里"每轮都调用 PetAction_Perform()"完全等价。
  *        本任务是长阻塞任务，它会占住调度器直到动作结束，这是既定行为。
  */
static Task tAction(TASK_IMMEDIATE,
                    TASK_FOREVER,
                    &PetAction_Perform,
                    &ts,
                    false);

/*==============================================================================
 * C 接口实现
 *============================================================================*/

/**
  * @brief  启动时基、配置并启能任务
  */
extern "C" void SchedulerApp_Init(void)
{
	/* 1ms 时基（TIM4），必须最先启动，任务的时间计算依赖它 */
	TS_Port_Init();

	/* OLED 刷屏不做赶工式补跑 */
	tFace.setSchedulingOption(TASK_INTERVAL);

	/* 使能任务（enable 之后任务才进入调度） */
	tFace.enable();
	tAction.enable();
}

/**
  * @brief  执行一次调度
  */
extern "C" void SchedulerApp_Run(void)
{
	ts.execute();
}

/*==============================================================================
 * 附：以后要加新任务时的模板
 *
 * 步骤 1：写一个 void(void) 的回调（想调用 C 函数就直接调用，已在 extern "C" 里）
 *
 *     static void MyTaskCallback(void)
 *     {
 *         // 这里必须"快速返回"，不要写 Delay_ms 之类的阻塞代码
 *     }
 *
 * 步骤 2：定义 Task 对象，周期用 xxx * TASK_MILLISECOND，次数用 TASK_FOREVER
 *
 *     static Task tMyTask(200 * TASK_MILLISECOND, TASK_FOREVER,
 *                         &MyTaskCallback, &ts, false);
 *
 * 步骤 3：在 SchedulerApp_Init() 里使能
 *
 *     tMyTask.enable();
 *
 * 其他常用能力（本移植已在 TS_Config.h 中打开）：
 *   - 单次任务        : 次数写 TASK_ONCE，回调里可用 isLastIteration() 判断
 *   - 任务整体超时    : tMyTask.setTimeout(1000);         // _TASK_TIMEOUT
 *   - 任务私有数据    : tMyTask.setLtsPointer(&myStruct); // _TASK_LTS_POINTER
 *   - 事件驱动/任务握手: StatusRequest + waitFor()        // _TASK_STATUS_REQUEST
 *   - 运行统计        : tMyTask.getRunCounter() / getOverrun() / ts.getCpuLoad()
 *   - 运行时改周期    : tMyTask.setInterval(...) / setIntervalNodelay(...)
 *
 * 完整文档：https://arkhipenko.github.io/ts
 *============================================================================*/
