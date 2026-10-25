# TaskScheduler → STM32F103C8T6 / Keil MDK(AC5) 移植说明

本目录是 [arkhipenko/TaskScheduler](https://github.com/arkhipenko/TaskScheduler)
（v4.1.0，BSD-3-Clause）在本工程中的落地版本，包含**上游源码**与**STM32 移植层**两部分。

---

## 1. 目录内容

| 文件 | 来源 | 说明 |
| --- | --- | --- |
| `TaskScheduler.h` | 上游原样 | 库的实现（含全部方法定义） |
| `TaskSchedulerDeclarations.h` | 上游 **+ 2 处 [TS-PORT] 回退** | 库的类声明（`Task` / `Scheduler` / `StatusRequest` …）；改动原因见第 5 节 |
| `TaskSchedulerDeclarations.h.orig` | 上游原样备份 | 未经修改的原始文件，用于随时 diff |
| `TaskSchedulerSleepMethods.h` | 上游原样 | 仅 AVR/Teensy 的空闲休眠汇编，本移植**不使用** |
| `LICENSE.txt` | 上游原样 | BSD-3-Clause 许可证 |
| `library.properties` | 上游原样 | Arduino 库描述文件，保留以便对照版本 |
| `TS_Config.h` | **新增** | 编译期选项的单一事实来源 |
| `TS_Port.h` / `TS_Port.c` | **新增** | 移植层：TIM4 1ms 时基、`millis` / `micros` / `yield` |
| `TS_TaskScheduler.cpp` | **新增** | 库在本工程中的**唯一实现编译单元**，并把时基钩子接到移植层 |
| `TS_TaskScheduler.h` | **新增** | 业务代码（`.cpp`）的统一入口 |
| `README_PORT.md` | **新增** | 本文件 |

> 上游的 `src/TaskScheduler.cpp` 只有一个作用：在定义了 `_TASK_HEADER_AND_CPP` 时
> `#include <TaskScheduler.h>`。本移植改用 `TS_TaskScheduler.cpp` 承担这个角色
> （并且额外定义时基钩子），因此没有把它复制过来，避免出现两个空的编译单元。

---

## 2. 为什么要单独一个 `.cpp` 来编译库

上游库在 `_TASK_NON_ARDUINO` 模式下把时基钩子声明成**内部链接**的静态函数：

```cpp
static unsigned long _task_millis();
#if defined(_TASK_SLEEP_ON_IDLE_RUN) || defined(_TASK_TIMECRITICAL)
static unsigned long _task_micros();
#endif
static void _task_yield();
```

`static` 意味着：**每个包含了 `TaskScheduler.h` 的编译单元都必须自己提供一份定义**，
否则链接时报 `undefined symbol`。

因此本移植采用「声明 / 实现分离」：

```
业务 .cpp  ──包含──>  TS_TaskScheduler.h ──> TS_Config.h
                                          └─> TaskSchedulerDeclarations.h   （纯声明）

TS_TaskScheduler.cpp ──包含──> TS_Config.h
                              ├─ TS_Port.h
                              ├─ TaskScheduler.h        （唯一包含实现的编译单元）
                              └─ 定义 _task_millis() / _task_micros() / _task_yield()
```

好处：上游源码**零修改**，同时支持任意数量的 `.cpp` 文件共用同一个调度器。

---

## 3. 时基（TIM4）

| 项目 | 取值 |
| --- | --- |
| 时钟源 | APB1 定时器时钟 72MHz（APB1=36MHz，定时器时钟 ×2） |
| 预分频 | 72-1 → 计数频率 1MHz |
| 重装载 | 1000-1 → 更新中断 1kHz（1ms） |
| 中断优先级 | 抢占 3 / 响应 0（NVIC 分组 2），低于语音(1)、蓝牙(2)、TIM3(0) |

- `TS_Millis()` = 中断里自增的软件计数器（32 位，约 **49.7 天**回绕）
- `TS_Micros()` = 计数器 × 1000 + `TIM4->CNT`（1MHz 计数，`CNT` 本身就是"本毫秒内已过的微秒数"）。
  因为是 `ms × 1000`，32 位下约 **71.6 分钟**回绕一次。
  库只把它用于 `getStartDelay()` / `getOverrun()` 这类**差值**计算，
  无符号环绕相减仍然正确，所以不影响功能；
  但如果你自己拿 `TS_Micros()` 做长时间跨度的时间戳比较，要留意这个 71.6 分钟。
- `TS_Yield()` = `__NOP()`（裸机协作式单线程无需让步，留作接入 RTOS / `__WFI()` 的挂钩）

### 为什么不能用别的定时器

| 定时器 | 现状 |
| --- | --- |
| `TIM2` | `Servo.c` 的舵机 PWM |
| `TIM3` | `Timer.c` 的 50Hz 舵机 PWM，其更新中断驱动呼吸灯与电量检测 |
| `SysTick` | `Delay.c` 的 `Delay_us()` 每次调用都会重配置它并在结束时关闭，**不能**当连续时基 |
| `TIM1` | 高级定时器，保留给将来需要互补输出/死区控制的场合 |

### `TS_Micros()` 的进位竞争处理

`TIM4->CNT` 在更新事件时被硬件清零，而软件计数器要等中断服务函数执行时才自增。
在"CNT 已清零、计数器还没加"的窗口里直接相减会得到偏小 1000µs 的结果。

处理办法：读之前、读之后各检查一次 `UIF`，并核对软件计数器有没有被改动，
只要有一处不一致就整体重读；
中断服务函数内部是**先自增计数器、后清 `UIF`**，因此 `UIF=0` 时两者必然对齐。
**不要调换这两行的顺序**，否则 `TS_Micros()` 会出现约 1ms 的跳变。

重试次数有上限（`TS_MICROS_MAX_RETRY`，默认 8 次）。这一点很关键：
若在抢占优先级高于 TIM4 的中断里、或全局关中断期间调用，`UIF` 永远不会被清掉，
无界的循环会**死等**导致系统挂死；有了上限之后最坏只是返回误差 ≤1ms 的近似值。

---

## 4. 编译期选项（`TS_Config.h`）

**已开启**

| 选项 | 作用 |
| --- | --- |
| `_TASK_NON_ARDUINO` | 必选：不含 `<Arduino.h>`，时基由移植层提供 |
| `_TASK_STATUS_REQUEST` | 任务间的事件/握手（语音、蓝牙中断通知任务时很有用） |
| `_TASK_LTS_POINTER` | 任务私有 `void*` 数据指针 |
| `_TASK_WDT_IDS` | 任务 ID + 控制点（`setControlPoint()`） |
| `_TASK_TIMECRITICAL` | `getStartDelay()` / `getOverrun()` / CPU 占用统计 |
| `_TASK_TIMEOUT` | 单任务整体超时 |
| `_TASK_SCHEDULING_OPTIONS` | `TASK_SCHEDULE` / `TASK_SCHEDULE_NC` / `TASK_INTERVAL` |
| `_TASK_EXPOSE_CHAIN` | 遍历任务链 |

**已关闭及原因**：见 `TS_Config.h` 第三节，每一条都写了不开的理由。

---

## 5. 对上游源码做的唯一修改：C++11 → C++03 回退

上游 v4.1.0 使用了两个 **C++11** 语法，而 ARM Compiler 5（armcc 5.06）默认是 **C++03** 模式：

| 位置 | 上游原文 | 报错 |
| --- | --- | --- |
| `TaskSchedulerDeclarations.h`（枚举） | `enum _task_scheduler_state_t : uint8_t {` | `Error: #40: expected an identifier` |
| `TaskSchedulerDeclarations.h`（Scheduler 类） | `Scheduler(const Scheduler&) = delete;` 等两行 | `Error: #65: expected a ";"` |

### 为什么不用 `--cpp11` 编译开关

`armcc` **没有**"只对 C++ 文件生效"的 C++11 开关：

- `--cpp11` 隐含 `--cpp`，会让**所有 `.c` 文件也按 C++ 编译**。
  实测后果：`HardWare/OLED.c` 里的 `atan2(Y, X)` 直接报
  `Error: #308: more than one instance of overloaded function "atan2" matches`
  —— 整个工程 40 多个 C 文件的语言都被改掉了，风险不可接受。
- 把 `--c99` 放在 `--cpp11` **之后**虽然能让 `.c` 回到 C 模式，
  但同时会把 `.cpp` 从 C++11 降回 C++03，等于白加。

### 实际做法（两处 [TS-PORT] 标记）

1. `enum _task_scheduler_state_t : uint8_t { ... }` → 去掉底层类型说明符。
   该枚举取值只有 `0/1/2`，armcc 默认就会选最小可用宽度，
   所以 `Scheduler` 对象的内存布局与上游一致。
2. `Scheduler(const Scheduler&) = delete;` → 改为 C++03 惯用法：
   声明为 `private` 且**不提供定义**。外部代码尝试复制调度器时
   同样会在编译期被拒绝，语义等价。
   ⚠️ 不要为这两个函数补上定义。

改动处都用 `[TS-PORT]` 注释块标注了原始写法与原因；
上游原始文件另存为 `TaskSchedulerDeclarations.h.orig`，可随时 diff。

这样处理后：
- Keil 工程**不需要任何特殊编译开关**，`MiscControls` 保持工程原有的 `--no-multibyte-chars`；
- 同一份源码也能在 GCC（`-std=c++03` 与 `-std=c++11` 均已实测）、EIDE、IAR 下编译；
- 已用宿主机 g++ 对 C++03 / C++11 两种模式跑了同一套调度行为仿真测试，结果一致。

> 若将来升级上游库又引入了新的 C++11 语法，请在 `TS_Config.h` 描述的基础上，
> 按同样的方式继续回退；若回退成本过高，则考虑把工程切到 AC6（armclang）并开启
> `--cpp11`（`Project.uvprojx` 里的 `<uAC6>0</uAC6>` 改成 1），但那会牵动整个工程的重编译。

---

## 6. Keil 工程文件的两个要点

### 6.1 C++ 源文件必须用 `<FileType>8`

`Project.uvprojx` 里文件节点的 `<FileType>` 取值**不是**按扩展名自动判断的：

| 取值 | 含义 | 用错的后果 |
| --- | --- | --- |
| 1 | C 源文件 | `.cpp` 会被**当成 C 编译**，报 `identifier "class" is undefined` |
| 2 | 汇编 | — |
| 5 | 文本（头文件） | — |
| **8** | **C++ 源文件** | — |

因此 `TS_TaskScheduler.cpp` 与 `SchedulerApp.cpp` 都写成 `<FileType>8</FileType>`。

> **以后自己新建 `.cpp` 文件时**：在 Keil 里是右键分组 → `Add Existing Files…`，
> µVision 会自动写对类型；如果手工改 `.uvprojx`，请务必用 `8`。

### 6.2 需要手工同步的内容

- `Project.uvprojx`：新增了 `TaskScheduler` 分组、`User` 分组里加了
  `SchedulerApp.cpp/.h`、包含路径加了 `.\TaskScheduler`。
- `Project.uvoptx` 只保存 µVision 的界面状态（展开状态等），
  外部修改 `.uvprojx` 后它与文件列表暂时不同步是正常的，
  用 µVision 打开工程时会自动补齐，不影响编译。

---

## 7. 在本工程中的用法

```c
/* User/main.c —— 初始化最后 */
SchedulerApp_Init();      /* 启动 1ms 时基 + 使能任务 */

while (1) {
    SchedulerApp_Run();   /* 执行一次调度 */
}
```

任务定义在 `User/SchedulerApp.cpp`，新增任务的模板也写在那个文件末尾。

### 重要取舍：硬实时任务留在中断里

`TaskScheduler` 是**协作式**调度器，只能在任务主动返回的位置切换，**不能抢占**。
而 `PetAction_Perform()` 内部是阻塞式的（`Action_advance` / `Action_Swing` /
`Action_SwingTail` 等连续 `Delay_ms`，甚至无限循环，靠串口中断改 `Action_Mode` 打断）。

呼吸灯的亮度正是靠"每次中断把 PWM 比较值加减一个固定步长"累积出来的
（现在 40ms 一次、每步 100，走完 0↔20000 需要 200 步 ≈ 8 秒）。
如果把它搬进任务链，动作执行期间它会被饿死 → 呼吸灯卡住、电量停止采样。

因此：

- `Face_Config()` / `PetAction_Perform()` → 迁移到调度器任务
- `GetCur_Power()` / `LED_Breathing()` → **保留在 TIM3 中断**（硬实时）

将来若把 `PetAction.c` 重写成非阻塞状态机，这两项才适合搬进任务链。

### 关于 TIM3 的分频

`Timer.c` 里 `TIM_Period = 20000`（1MHz 计数 → 20ms）**同时决定了两件事**：

1. 舵机 PWM 周期必须是 20ms（50Hz）
2. 更新中断的频率是 50Hz

所以**不能**靠改 `TIM_Period` 来降低中断频率（那会把舵机 PWM 变成 20Hz）。
本移植改为在 `TIM3_IRQHandler` 里做 2 分频，得到 **40ms** 的有效周期。

> **呼吸灯节奏**：调用周期翻倍，步长不变的话呼吸就会自然慢一倍。
> 按需求这里明确把呼吸灯调慢为 **8s 渐亮 + 8s 渐暗 + 800ms 停顿**（原来是 4s + 4s + 400ms）。
> 目标节奏被写成 `Led_Breathing.c` 顶部的 `LED_BREATH_FADE_MS` / `LED_BREATH_HOLD_MS`
> 两个常量，步长由它们推算，所以以后调快调慢只改这两个数；
> 想恢复移植前的 4s 节奏，把它们改成 `4000U` / `400U` 即可。
> 文件里还加了 `#error` 编译期检查，防止有人填了不能整除的值导致亮度台阶不均匀。

> **副作用**：`GetCur_Power()` 的 100 个样本窗口由 2s 变为 4s，即电量显示刷新变慢。
>
> **恢复 2s 窗口的正确做法**：把 `PowerDetection.c` 顶部的
> `BATTERY_FILTER_SAMPLES` 从 `100U` 改成 `50U`。
> 该宏同时用于"攒满判定"和"求平均的除数"，改一处即可。
>
> ⚠️ 不要只改 `Battery_num<=100` 这一处比较 —— 求平均的行里还硬编码着 `/100`，
> 只改比较会得到"除以 100 却只累加了 40 个样本"的错误结果。

### 顺带修掉的既有 bug：`temp1` 溢出

`GetCur_Power()` 的累加器 `temp1` 原本是 `uint16_t`。单个样本最大约 1020，
累加 100 个可达 102000，远超 65535。

正常电池电压下单样本只有 0~120（`Face_Config.c` 正是按这个范围显示），
累加 100 个才 12000，所以平时不会溢出、看不出来；
但电池未接或 ADC 输入悬空导致读数偏高时（ADC > 2963），`temp1` 会回绕，
`CurBattery` 直接显示成错误数字。

现已改为 `uint32_t`（`Variable.h` / `Variable.c`），正常区间的显示结果与修改前完全一致。

---

## 8. 升级上游库的步骤

1. 用新版本覆盖 `TaskScheduler.h`、`TaskSchedulerDeclarations.h`、
   `TaskSchedulerSleepMethods.h`、`LICENSE.txt`、`library.properties`。
2. **不要**动 `TS_*.h` / `TS_*.cpp` / `User/SchedulerApp.*`。
3. 用 `TaskSchedulerDeclarations.h.orig` 与旧版做 diff，确认上游都改了什么。
4. **重新施加第 5 节的两处 `[TS-PORT]` C++03 回退**（覆盖会把它们冲掉）。
   做法：在新文件里搜 `enum _task_scheduler_state_t` 与 `= delete`。
5. 检查新版本是否引入了新的编译期选项 → 相应更新 `TS_Config.h`。
6. 检查新版本是否用了新的 C++11 语法（如 `nullptr`、`auto`、变参模板）：
   - 能简单回退的就照第 5 节的方式回退；
   - 回退成本过高则改用 AC6（armclang），把 `Project.uvprojx` 的
     `<uAC6>0</uAC6>` 改成 1 —— 但这会让**整个工程**重新编译，
     且需重新确认 AC5/AC6 的差异（`--no-multibyte-chars` 等在 AC6 下不存在）。
7. 用 Keil 重新编译一次（`Project → Rebuild all target files`），
   确认仍然是 **0 Error(s), 0 Warning(s)**。