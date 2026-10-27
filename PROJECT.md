# 智能桌宠（Desktop Pet）· 项目说明

> 一块 STM32F103C8T6 驱动的四足桌宠：会走、会摇、会做十几种动作，OLED 上有八种表情，
> 能听懂语音指令，也能被手机 App 遥控，还能把手机取到的天气显示在自己屏幕上。

本文是项目的总说明，涵盖硬件接线、软件架构、协议、上位机、编译烧录和踩过的问题。
App 的使用细节另见 [AndroidApp/README.md](AndroidApp/README.md)，版本记录见
[AndroidApp/CHANGELOG.md](AndroidApp/CHANGELOG.md)。

---

## 目录

1. [项目概览](#1-项目概览)
2. [硬件与引脚](#2-硬件与引脚)
3. [软件架构](#3-软件架构)
4. [固件模块详解](#4-固件模块详解)
5. [动作与表情](#5-动作与表情)
6. [通信协议](#6-通信协议)
7. [天气链路](#7-天气链路)
8. [Android App](#8-android-app)
9. [编译与烧录](#9-编译与烧录)
10. [踩过的坑与修复记录](#10-踩过的坑与修复记录)
11. [目录结构](#11-目录结构)
12. [已知限制与后续可做](#12-已知限制与后续可做)

---

## 1. 项目概览

| 项目 | 说明 |
| --- | --- |
| 主控 | STM32F103C8T6，Cortex-M3 @72MHz，64KB Flash / 20KB RAM |
| 执行机构 | 4× SG90 **180° 位置舵机**（四条腿，对角步态）+ 1× 尾巴舵机 |
| 显示 | 0.96″ OLED 128×64，**软件 I2C**（PB8 / PB9） |
| 交互 | 语音识别模块（USART1，9600）+ BLE 蓝牙模块（USART3，115200）+ 手机 App |
| 电源 | 锂电池经 1/4 分压接 PA4，ADC 采样估算电量 |
| 固件规模 | Code 18.9KB + RO 10.3KB ≈ **28.8KB / 64KB（45%）**；RAM 132 + 2924 ≈ **3.0KB / 20KB（15%）** |
| 调度器 | TaskScheduler v4.1.0（BSD-3，header-only，C++03）移植，TIM4 1ms 时基 |
| 上位机 | Android「桌宠遥控」v1.1，单 Activity、零第三方依赖，APK 41KB |

### 能力清单

- 16 种动作：站立/蹲下/趴下/放松趴下、前进/后退/左转/右转、摇摆（定时或定次）、
  摇尾巴、前跳/后跳、打招呼、伸懒腰、后腿拉伸
- 8 种表情：睡觉、瞪眼、开心、狂热、非常开心、大眼睛、天气
- 双通道指令：语音模块说一句话是一个 ASCII 字节；手机 App 点一个按钮也是一个字节
- 天气显示：手机定位取 Open-Meteo 数据 → 蓝牙下发帧 → OLED 显示「晴 24~29C 湿度60%」
- 反向通道：语音说「天气」时，桌宠主动经蓝牙向手机要一次天气
- 电池电量实时显示（叠加在表情右上角）
- 双 LED 呼吸灯（8 秒渐亮 + 8 秒渐暗 + 800ms 停顿）
- 舵机中位微调（软件补偿装配误差），可通过蓝牙进入标定模式现场调

---

## 2. 硬件与引脚

### 2.1 引脚分配

| 引脚 | 外设/功能 | 接什么 | 备注 |
| --- | --- | --- | --- |
| PA0 | TIM2_CH1 | S1 左前腿舵机 | 500~2500us → 0~180° |
| PA1 | TIM2_CH2 | S2 右前腿舵机 | 底层做 (3000 − pulse) 镜像 |
| PA2 | TIM2_CH3 | S3 左后腿舵机 | |
| PA3 | TIM2_CH4 | S4 右后腿舵机 | 底层做镜像 |
| PA4 | ADC1_IN4 | 电池分压 | 4× 分压，见 2.2 |
| PA6 | TIM3_CH1 | 尾巴舵机 | 与四条腿同一套脉宽语义 |
| PA9 | USART1_TX | 语音模块 RX | 9600 8N1 |
| PA10 | USART1_RX | 语音模块 TX | 抢占优先级 1 |
| PB0 | TIM3_CH3 | LED1 | PWM 调亮度 |
| PB1 | TIM3_CH4 | LED2 | PWM 调亮度 |
| PB8 | GPIO 开漏 | OLED SCL | 软件 I2C，从机地址 0x78 |
| PB9 | GPIO 开漏 | OLED SDA | |
| PB10 | USART3_TX | BLE 模块 RX | 115200 8N1 |
| PB11 | USART3_RX | BLE 模块 TX | 抢占优先级 2 |

> **为什么 TIM2 和 TIM3 都用 50Hz**：SG90 这类模拟舵机要的是 20ms 周期的 PWM 脉宽，
> 而 TIM3 的更新中断还兼任「20ms 硬实时任务」的节拍源。两者共用一个时基避免了
> 再多占一个定时器，代价是节拍只能是 20ms（需要更慢的节奏就在中断里分频）。

### 2.2 定时器与外设资源分配

| 资源 | 用途 | 关键参数 |
| --- | --- | --- |
| TIM2 | 四条腿 PWM | PSC 71，ARR 19999 → 1MHz/20000 = **50Hz**，4 通道 |
| TIM3 | 尾巴 PWM + 双 LED PWM + 20ms 节拍 | 同样 50Hz；CH1 尾巴，CH3/CH4 LED；更新中断分频 2 → **40ms** 跑电量/呼吸灯 |
| TIM4 | TaskScheduler 时基 | PSC 71，ARR 999 → **1ms** 更新中断，抢占优先级 3（最低） |
| SysTick | 被 `Delay.c` 独占 | 不用于调度，`Delay_us` 会改写 `SysTick->LOAD` |
| USART1 | 语音模块 | 9600 8N1，收中断 → `PetAction_Judgmengt(0)` |
| USART3 | BLE 模块 | 115200 8N1，收中断 → 缓冲，20ms 判定 → `PetAction_Judgmengt(1)` |
| ADC1 | 电池 | IN4 / ADCCLK÷6 / 55.5 周期采样，单次转换 |

**电量换算**（`PowerDetection.c`）：

```c
Battery_Value = (3.3f * 4 * Get_ADC() / 4096) * 100 - 300;
```

单节锂电池 3.0~4.2V，乘 4 换算到 ADC 端的 12V 量程，再压到 0~100 的百分比。
采样 100 次求平均做滤波（40ms 一次 → 4 秒窗口）。

### 2.3 中断优先级（NVIC 分组 2）

| 中断 | 抢占 | 子优先级 | 说明 |
| --- | --- | --- | --- |
| TIM3_IRQn | 0 | 0 | 最高。电量采样 + 呼吸灯，必须严格周期 |
| USART1_IRQn | 1 | 1 | 语音指令，要立刻响应 |
| USART3_IRQn | 2 | 2 | 蓝牙收字节（只入缓冲） |
| TIM4_IRQn | 3 | 0 | 最低。1ms 时基 + 20ms 蓝牙指令判定 |

优先级链：**TIM3 > 语音 > 蓝牙 > 调度器**。这样设计的两个原因：

1. TIM3 最短 20ms 一次，呼吸灯的亮度是靠"每次加 100"累积出来的，被拖慢就会抖；
2. 蓝牙指令判定放在 TIM4 中断里（见 3.4），它必须能被新到的串口字节抢占。

---

## 3. 软件架构

### 3.1 分层

```
应用层    PetAction.c（动作）  Face_Config.c（表情）  Weather.c（天气）
          SchedulerApp.cpp（任务定义 + 调度入口，C++）
──────────────────────────────────────────────────────────────
调度层    TaskScheduler v4.1.0（header-only, C++03）
          TS_Port.c（TIM4 1ms 时基，C 接口）
──────────────────────────────────────────────────────────────
驱动层    Servo.c / PWM.c（TIM2/TIM3 PWM）   OLED.c（软件 I2C 128×64）
          BlueTooth.c（USART3）  Voice.c（USART1）  AD.c / PowerDetection.c
          Led_Breathing.c  Timer.c  Delay.c
──────────────────────────────────────────────────────────────
硬件      STM32F103C8T6 + SG90×5 + OLED + 语音模块 + BLE 模块 + 电池
```

### 3.2 从主循环到任务

改造前（原始结构）：

```c
while (1) {
    Face_Config();        // 刷 OLED 表情 + 电量
    PetAction_Perform();  // 执行舵机动作（内部是阻塞式的）
}
TIM3_IRQHandler() { GetCur_Power(); LED_Breathing(); }   // 20ms 硬实时
```

改造后：

```c
SchedulerApp_Init();      // 启动 TIM4 1ms 时基 + 使能任务
while (1) {
    SchedulerApp_Run();   // ts.execute()
}
```

两个任务：

| 任务 | 周期 | 回调 | 调度策略 |
| --- | --- | --- | --- |
| `tFace` | 30ms | `Face_Config()` | `TASK_INTERVAL`（不追赶补跑） |
| `tAction` | `TASK_IMMEDIATE` | `PetAction_Perform()` | 每个调度 pass 都跑一次 |

`tFace` 用 `TASK_INTERVAL` 是刻意的：默认的 `TASK_SCHEDULE` 会记住原始时间点、
被阻塞后连续补跑，对"刷屏"这种过期就过期的任务毫无意义，只会白占 CPU。

### 3.3 为什么电量检测和呼吸灯留在中断里

这是整个项目**最重要的一条设计约束**。

TaskScheduler 是**协作式**调度器：只能在"任务主动返回"的位置切换任务，不能抢占
正在执行的代码。而 `PetAction_Perform()` 内部是阻塞式实现（连续 `Delay_ms`，
甚至靠串口中断改 `Action_Mode` 来打断），一进去就是几十毫秒到几十秒。

一旦动作开始阻塞，任务链上排在后面的任务在动作结束前都不会执行。呼吸灯的亮度
靠"每 40ms 调一次、每次加减 100"累积，被饿死就卡住不动；电量采样同理。
所以这两项属于硬实时周期任务，正确位置就是定时器中断。

> 等以后把 `PetAction.c` 重写成非阻塞状态机，它们才适合搬到任务链里。

### 3.4 阻塞动作的三个补救手段

既然动作注定要阻塞，就把它造成的三处副作用分别补上：

| 副作用 | 补救 | 位置 |
| --- | --- | --- |
| 呼吸灯/电量停摆 | 留在 TIM3 中断，不进任务链 | `main.c` / `Led_Breathing.c` |
| 动作期间收不到新指令 | 蓝牙指令判定放进 TIM4 中断，20ms 一次 | `TS_Port.c` → `BlueTooth_Poll()` |
| 动作期间表情不刷新 | 动作循环里插 `Face_Service()`（只在表情变了才重画） | `PetAction.c` + `Face_Config.c` |

---

## 4. 固件模块详解

| 文件 | 行数 | 职责 |
| --- | --- | --- |
| `User/main.c` | 484 | 初始化顺序、主循环、TIM3 中断、舵机测试/标定模式 |
| `User/SchedulerApp.cpp` | 157 | TaskScheduler 任务定义与启停，C 接口实现 |
| `HardWare/Servo.c` | 140 | 角度→脉宽映射、左右腿镜像、中位微调 |
| `HardWare/PWM.c` | 62 | TIM2/TIM3 通道配置，尾巴与 LED 的 PWM 输出 |
| `HardWare/PetAction.c` | 933 | 全部动作实现 + 指令分发（两个通道） |
| `HardWare/Face_Config.c` | 155 | 8 种表情、天气页、电量浮层、`Face_Service()` |
| `HardWare/OLED.c` / `OLED_Data.c` | 1415 / 931 | 江协科技 OLED 驱动（软件 I2C）+ 字模 + 7 张全屏位图 |
| `HardWare/BlueTooth.c` | 143 | USART3 初始化、间隙过滤缓冲、`BlueTooth_Poll()` |
| `HardWare/Voice.c` | 58 | USART1 初始化，收字节直接分发 |
| `HardWare/Weather.c` | 136 | `AA 55` 天气帧状态机、天气类型→中文 |
| `HardWare/PowerDetection.c` | 64 | 电量换算与 100 点滑动平均 |
| `HardWare/Led_Breathing.c` | 97 | 呼吸灯状态机（渐亮/渐暗/停顿） |
| `HardWare/Timer.c` | 74 | TIM3 初始化（与 PWM.c 配合） |
| `HardWare/Variable.c` / `.h` | 47 | 全部全局变量与可调宏 |
| `HardWare/Delay.c` | 41 | 基于 SysTick 的 us/ms 延时 |
| `TaskScheduler/*` | 1800+ | 上游库 + 移植层 `TS_Port.c`（195 行） |

### 4.1 Servo.c —— 角度与脉宽

SG90 是 180° 位置舵机：给一个固定脉宽，它就转到对应角度并自己闭环保持。
所以动作代码里"转到 45°"只是一次 `TIM_SetCompareX`，`Delay_ms` 只是留给它转过去的时间。

```c
Pulse = Angle / 180 * 2000 + 500;      // 500us↔0°, 1500us↔90°, 2500us↔180°
```

- **左右腿镜像**：`Servo_Angle2/4` 内部做 `3000 − pulse`，让同一个角度参数
  在左右两侧摆出对称姿态（机械上舵机是镜像安装的）。
- **中位微调**：四条腿各有一个 `int8_t` 偏移（出厂值 S1 +21 / S2 +22 / S3 0 / S4 −20），
  叠加在镜像换算**之前**，所以对 2/4 号腿同样有效。换算约 11.1us/度，建议 ±40° 以内。
- **上电必须输出 1500us**：PWM1 模式下 CCR=0 是恒定低电平，舵机收不到有效脉宽会失控乱转，
  所以初始化就把 `TIM_Pulse` 设成 1500。

### 4.2 PetAction.c —— 动作与指令

- 每个动作是一个阻塞函数（`Action_advance` / `Action_Swing` / …），由
  `PetAction_Perform()` 按 `Action_Mode` 分派。
- 动作执行中靠 `Action_Mode` 的变化来打断自己（`while(Action_Mode==4)`），
  所以"换动作"不需要强行中断，改个变量就行。
- 蓝牙通道会置 `Sustainedmove=1`（一直走，直到下一条命令）；
  语音通道置 0（单次执行，跑完 `Chongfunumber` 个周期就停）。
- 语音通道在一串动作结束后补 2 秒等待，见 5.3。

### 4.3 Face_Config.c —— 表情

- 按 `Face_Mode` 分支：0~6 是全屏位图，7 是天气页（汉字 + 数字，实时取值）。
- 电量浮层：`if(Battery_Bit && Face_Mode != 7)` 在右上角画 `Power:xx`。
  （天气页不画，否则会盖住温度行。）
- 末尾统一 `OLED_Update()`；一次全屏刷新是 1024 字节软件 I2C，量级 **20~30ms**。
- `Face_Service()`：只在 `Face_Mode != Face_DrawnMode` 时重画一次，供阻塞动作调用。

### 4.4 BlueTooth.c —— 蓝牙通道

ISR 只做两件事：喂天气状态机、把字节丢进缓冲。真正的判定在 `BlueTooth_Poll()`：

```c
if (n == 1U) { RxData_Blue = b; PetAction_Judgmengt(1); }
/* n == 0：没字节；n >= 2：一簇，丢弃 —— 见 6.2 */
```

### 4.5 Weather.c —— 天气帧

四态状态机 `IDLE → HEAD2 → LEN → PAYLOAD → CHECK`，任何阶段收到非预期的 `0xAA`
都会重新同步成新帧头，所以**不会永久卡死**（最坏吞掉几个字节就恢复）。

校验：`XOR = LEN ^ CODE ^ TMIN ^ TMAX ^ HUM`（5 项），长度必须是 4 才采信。

### 4.6 PowerDetection.c —— 电量滤波

```c
if (++Battery_num <= 100)  temp1 += GetBattery();      // temp1 必须是 uint32_t
else { CurBattery = temp1 / 100; temp1 = 0; Battery_num = 0; }
```

`temp1` 原本是 `uint16_t`：单个样本满量程约 1020，累加 100 个可达 102000，
超过 65535 会回绕。正常电压下看不出来，但电池没接/分压电阻虚焊（ADC 读到悬空高值）时
会显示出离谱的数字。已改成 `uint32_t`。

### 4.7 Led_Breathing.c —— 呼吸灯

由 TIM3 中断驱动（分频后 40ms 一次），目标节奏是 **8s 渐亮 + 8s 渐暗 + 800ms 停顿**：

```c
#define LED_BREATH_FADE_MS   8000U
#define LED_BREATH_HOLD_MS    800U
#define LED_BREATH_STEP      (20000 / (8000/40))   // = 100
```

想改快慢只动这两个宏，步长自动换算（编译期有整除检查）。
渐亮/渐暗都先判断再加/减，防止越过上限后回绕。

### 4.8 Delay.c —— 被征用的 SysTick

`Delay_us` 直接改写 `SysTick->LOAD` 并轮询计数标志，`Delay_ms` 循环调用 `Delay_us(1000)`。
这带来一个隐含限制：**SysTick 不能再用作别的用途**（比如 HAL 的 1ms 时基）。
本工程的 1ms 时基因此改用 TIM4。

### 4.9 TaskScheduler 移植层

- `TS_Config.h`：只开需要的特性（`_TASK_SLEEP_ON_IDLE_RUN` 关闭等），保持 RAM 占用小。
- `TS_Port.c`：TIM4 1ms 时基（`s_tsTick` 自增）、`millis()/micros()` 实现、
  以及新增的 20ms 蓝牙指令判定。
- 库是 header-only C++03，只能被**一个** C++ 编译单元包含 —— 这就是
  `TS_TaskScheduler.cpp` 存在的意义（它单独包含库，其它文件只包含 `TS_TaskScheduler.h`）。
- `SchedulerApp.cpp` 里所有 C 头文件必须用 `extern "C" {}` 包住，
  否则 armcc 会按 C++ 规则做名字修饰，链接时找不到符号。

---

## 5. 动作与表情

### 5.1 动作模式（`Action_Mode`）

| 值 | 动作 | 值 | 动作 |
| --- | --- | --- | --- |
| 0 | 放松趴下 | 8 | 摇摆 |
| 1 | 蹲下 | 9 | 摇尾巴（待机） |
| 2 | 直立 | 10 | 前跳 |
| 3 | 趴下 | 11 | 后跳 |
| 4 | 前进 | 12 | 直立2 |
| 5 | 后退 | 13 | 打招呼 |
| 6 | 左转 | 14 | 伸懒腰 |
| 7 | 右转 | 15 | 后腿拉伸 |

除"待机摇尾巴（9）"外，所有动作执行完都收敛到 9 或 2，再回到 9。

### 5.2 表情（`Face_Mode`）

| 值 | 表情 | 位图 | 触发场景 |
| --- | --- | --- | --- |
| 0 | 睡觉 | `Face_sleep` | 放松趴下 |
| 1 | 瞪大眼 | `Face_stare` | 蹲下、摇尾巴 |
| 2 | 开心 | `Face_happy` | 直立/前进/跳跃/调速到最快 |
| 3 | 狂热 | `Face_mania` | 到达调速极限 |
| 4 | 非常开心 | `Face_very_happy` | 摇摆、调速到最慢 |
| 5 | 大眼睛 | `Face_eyes` | 上电默认、直立 |
| 6 | 大眼睛 | `Face_eyes` | 打招呼/伸懒腰/拉伸（原 `Face_hello` 已停用） |
| 7 | 天气 | 实时绘制 | 显示天气 |

> `Face_hello` 的位图数据仍保留在 `OLED_Data.c` 里但已无引用，删掉可省约 1KB Flash。

### 5.3 动作结束后的 2 秒等待（仅语音通道）

```c
if ((ModeBefore != 9U) && (Action_Mode == 9U) && (Sustainedmove == 0U))
{
    Delay_ms(2000);          // ACTION_TAIL_DELAY_MS
}
```

判断依据是"进来时不是 9、出去时变成 9"，所以一串动作（打招呼 → 站好 → 待机）
只在最后停一次。目的：实测有些动作比语音播报短，动作一结束就执行下一条会把语音打断。

### 5.4 摇摆的两种计时

| 触发 | 计时方式 | 实际时长 |
| --- | --- | --- |
| 0x37（定次） | `SwingRepeatnumber`=3 个来回 | 242 步/来回 × 6ms ≈ **4.4 秒** |
| 0x52~0x55（定时，仅语音） | `SwingSeconds` = 26/27/16/15 | 累计到设定秒数为止 |

秒数不是靠定时器，而是数的 `Delay_ms(SwingDelay)`，所以按过「摇摆加速」（改 `SwingDelay`）
之后秒数依然准确。摇摆结束会 `Legs_All(90)` 站好并把 `SwingSeconds` 清零。

---

## 6. 通信协议

### 6.1 蓝牙 / 语音：单字节指令

一次操作 = 一个 ASCII 字节，**无 CRLF、无帧头**。

| 字节 | 字符 | 指令 | 字节 | 字符 | 指令 |
| --- | --- | --- | --- | --- | --- |
| 0x29 | `)` | 放松趴下 | 0x40 | `@` | 摇尾巴 |
| 0x30 | `0` | 蹲下 | 0x41 | `A` | 前跳 |
| 0x31 | `1` | 直立 | 0x42 | `B` | 后跳 |
| 0x32 | `2` | 趴下 | 0x43 | `C` | 打招呼 |
| 0x33 | `3` | 前进 | 0x44 | `D` | 开灯 |
| 0x34 | `4` | 后退 | 0x45 | `E` | 关灯 |
| 0x35 | `5` | 左转 | 0x46 | `F` | 呼吸灯开 |
| 0x36 | `6` | 右转 | 0x47 | `G` | 呼吸灯关 |
| 0x37 | `7` | 摇摆（3 个来回） | 0x48 | `H` | 伸懒腰 |
| 0x38 | `8` | 走路加速（300~550ms 循环） | 0x49 | `I` | 拉伸 |
| 0x39 | `9` | 摇摆加速（9~3ms/度 循环） | 0x50 | `P` | 显示电量（开关） |
| 0x52 | `R` | 摇摆 26s（仅语音） | 0x51 | `Q` | 显示天气 |
| 0x53 | `S` | 摇摆 27s（仅语音） | 0x54 | `T` | 摇摆 16s（仅语音） |
| 0x55 | `U` | 摇摆 15s（仅语音） | | | |

**两个通道的差异**：

| | 语音（USART1） | 蓝牙（USART3） |
| --- | --- | --- |
| `Sustainedmove` | 0（单次执行） | 1（一直做，直到下一条命令） |
| 0x52~0x55 | ✅ 支持 | ❌ 直接丢弃 |
| 2 秒尾等待 | 有 | 无 |
| 白名单 | `Command_IsValid()` | 同左 + 排除 0x52~0x55 |

### 6.2 抗干扰：20ms 间隙过滤

单字节协议有个天然的坑：**任何一个字节都可能被当成命令**。BLE 模块（尤其 AT 固件的）
在手机连上/断开时会往串口吐状态字符，比如 `CONNECTED` / `DISCONNECTED`，
这些字母里就藏着合法指令码：

| 状态串里的字符 | 被当成 |
| --- | --- |
| `C` 0x43 / `D` 0x44 / `E` 0x45 / `I` 0x49 | 打招呼 / 开灯 / 关灯 / 拉伸 |
| `S` 0x53 / `T` 0x54 | **摇摆 27 秒 / 16 秒** |

`0x52~0x55` 加进来之后正好撞上 `S`/`T`，于是**一连接、一断开就自己摇 16~27 秒**。

两道闸：

1. **间隙过滤**：合法指令永远是「单个字节」，状态串是「一串字节」。按 20ms 一个窗口统计，
   一个窗口里只来 1 个字节才执行，0 个或 ≥2 个都放过。
   ISR 只入缓冲，判定在 `BlueTooth_Poll()`（TIM4 中断里 20ms 一次）。
2. **`0x52~0x55` 只走语音通道**：蓝牙分支直接 `return`。

代价：每条指令最多延迟 20ms（人感觉不出来）；副作用：20ms 内连点两次会被当成一簇丢掉
（手动点不出来）。

### 6.3 天气帧（手机 → 板子）

```
AA 55 04 CODE TMIN TMAX HUM XOR        共 8 字节
│  │  │  │    │    │    │   └─ XOR = 04^CODE^TMIN^TMAX^HUM
│  │  │  │    │    │    └───── 湿度 %
│  │  │  │    │    └────────── 最高温 + 40
│  │  │  │    └─────────────── 最低温 + 40
│  │  │  └──────────────────── 天气类型 0~9
│  │  └─────────────────────── payload 长度，固定 4
│  └────────────────────────── 帧头 2
└───────────────────────────── 帧头 1
```

温度用 `+40` 偏移是为了负数也能塞进一个字节（-40~+87℃）。

天气类型：0 晴 / 1 多云 / 2 阴 / 3 小雨 / 4 中雨 / 5 大雨 / 6 阵雨 / 7 雷阵雨 / 8 雪 / 9 雾。

> 注意别把「payload 长度 4」和「校验覆盖 5 项」搞混 —— 这是本项目踩过的一个坑（见第 10 节）。

### 6.4 反向通道（板子 → 手机）

板子只发一个字节：`0x57`（`WEATHER_REQ_SYNC`），含义是"请给我一份天气"。
仅在**语音**说「天气」时发出；App 收到后自动定位取数据并下发帧。

---

## 7. 天气链路

```
① 语音说「天气」（或 App 按「显示天气」）
        │
        ├─ 语音通道：BlueTooth_SendByte(0x57) ──► BLE ──► 手机
        │                                                   │
        │                                          App 定位 + 请求 Open-Meteo
        │                                                   │
        │                                          GATT 写 FFF2（8 字节帧）
        │                                                   ▼
        └─ 蓝牙通道：Face_Mode=7, Action_Mode=2 ◄──── BLE 模块 ──► USART3
                                                              │
                                                    Weather_OnByte() 状态机
                                                              │
                                                    Weather_Valid=1 等 4 个字段
                                                              │
                                                    Face_Config() case 7 绘制
                                                              ▼
                                              ┌────────────────────────────┐
                                              │  晴                        │
                                              │  24~29C                    │
                                              │  湿度60%                   │
                                              └────────────────────────────┘
```

没收到数据时显示 `NO DATA`；收到后每 30ms 刷新一次（`tFace`）。

数据源：Open-Meteo（免费、无需 Key），取当天 `weather_code` + `temperature_2m_min/max` +
`relative_humidity_2m`，WMO 天气码映射到 0~9。

---

## 8. Android App

### 8.1 结构

- **单 Activity**（`MainActivity.java`，1466 行），零第三方依赖，界面全部用代码构建
  （`activity_main.xml` 只是一个 `LinearLayout` 容器 + 顶部卡片）。
- BLE 部分用系统 API 直连：`BluetoothLeScanner` → `connectGatt` → 发现服务 →
  写 CCCD 开启 `FFF1` 通知 → 向 `FFF2` 写指令。
- **不需要系统配对**，扫到设备点一下就连。

### 8.2 界面分组

五张卡片：**状态 / 移动 / 动作 / 灯光与显示 / 调速与天气**。

- 状态（4 档）发一次就保持；动作（7 个）有始有终，做完自己回待机 —— 分区是为了让语义可见。
- 移动是**十字方向键**，中间「站定」复用 `0x31`。
- 配色「薄荷曼波」：奶白 `#F9F4EA` 打底，薄荷 `#CFE4DD` / 樱花粉 `#FCD3D5` / 摩卡棕 `#8D6C63`
  分区，另加奶油沙 `#EFDFCF`、鼠尾草 `#DCE7E0` 两个同族调和色。

### 8.3 构建（不用 Gradle）

`AndroidApp/build_apk.ps1` 把流程摊开成 6 步：

```
aapt 打包资源 → javac 编译 → d8 生成 dex → 写入 dex → zipalign 对齐 → apksigner 签名
```

工具链：JDK 8（javac）+ JRE 17（d8 需要 Java ≥11）+ build-tools 34.0.0 + platforms/android-35。
产物 `AndroidApp/DeskPetRemote.apk`。

> 注意：`build_apk.ps1` 是 UTF-8 **带 BOM**，改动时必须保持，否则 PowerShell 会把中文注释读乱。

### 8.4 版本

当前 **1.1**（`versionCode=11`，规则 = 主版本×10 + 次版本）。详见
[CHANGELOG.md](AndroidApp/CHANGELOG.md)。

---

## 9. 编译与烧录

### 9.1 Keil（主工程）

- 工程：`Project.uvprojx`，目标 `Target 1`，器件 **STM32F103C8**（IROM 64KB / IRAM 20KB）
- 编译器：**AC5 / armcc V5.06 update 5 (build 528)**
- 命令行全量重编：

```powershell
& 'J:\Keil5 MDK\UV4\UV4.exe' -r Project.uvprojx -j0 -o _build.log
# UV4 会立刻返回，必须轮询读日志（编码 28591）等它写完
```

- 产物：`Objects/Project.hex`（已在 `.gitignore` 里忽略，需要单独分发）
- 正常应输出 **0 Error / 0 Warning**，参考体积：
  `Code=18938 RO-data=10550 RW-data=132 ZI-data=2924`

### 9.2 EIDE（VS Code 工程）

- 配置在 `EIDE/.eide/eide.yml`，源码路径用 `../` 相对写法。
- **新增 .c 文件必须同时改两处**：`Project.uvprojx` 和 `eide.yml`，否则两个工具链会一个能编一个不能。

### 9.3 三种运行模式（`User/main.c`）

```c
#define SERVO_TEST_MODE  0
```

| 值 | 模式 | 说明 |
| --- | --- | --- |
| 0 | 正常运行 | **出货必须保持 0**，此时才启动调度器 |
| 1 | 舵机全行程扫掠 | 逐条腿 0→180→0，看有没有卡滞/干涉；跑完停在 1500us |
| 2 | 中位标定 | 蓝牙发 `1`~`4` 选腿、`+`/`-` 微调、`0` 归零；调完把数值抄回 `Servo.c` 的宏 |

> 测试模式 1/2 **不调用** `SchedulerApp_Init()`，所以动作任务不会跑，避免和测试程序抢 TIM2。
> 也正因如此，标定模式的指令读取走的是 `RxData_Blue` 轮询，`BlueTooth.c` 的 ISR 里
> 特意保留了一份写入（否则标定收不到蓝牙按键）。

### 9.4 烧录后自检清单

1. 上电：OLED 出现**大眼睛**，四条腿站稳 90°，LED 常亮
2. 语音说「前进」：走 2 个周期（约 8 秒）后停下，表情变化
3. 手机连上蓝牙：**不应有任何动作**（这正是本次修的 bug）
4. 手机按「摇摆」：摇 3 个来回（约 4.4 秒）
5. 语音说「天气」：手机自动取数，屏幕显示天气；随后按手机「摇摆」，
   表情应在 10ms 内切走，不用等摇完
6. 断开蓝牙：**不应有任何动作**

---

## 10. 踩过的坑与修复记录

| # | 现象 | 根因 | 修法 |
| --- | --- | --- | --- |
| 1 | 天气永远显示 `NO DATA` | 帧的 LEN 字段写成 5，而 payload 实际是 4 | `WEATHER_PAYLOAD` 统一改 4（固件/App/预览页三处） |
| 2 | 负温度显示成 `-2C` | 单位字符盖掉了个位数 | 自写 `Face_NumAppend`，按实际位数输出 |
| 3 | 温度显示 `05` / `00` | `OLED_ShowNum` 按固定长度取位、高位补零 | 同上，改用字符串拼接 |
| 4 | 电量偶发显示离谱数字 | 累加器 `temp1` 是 `uint16_t`，异常高 ADC 值使其回绕 | 改 `uint32_t` |
| 5 | 按「摇摆加速」会连带翻转摇尾巴 | `case 0x39` 缺 `break`，贯穿到 `0x40` | 补 `break` |
| 6 | 走一两个周期后动作跑几十小时 | `PAnumbers` 是 `uint16_t`，减到 0 再减回绕成 65535 | 减之前判 0 |
| 7 | 任意乱码字节都会让桌宠持续运动 | 蓝牙通道收到任何字节都置 `Sustainedmove=1` | 加白名单 `Command_IsValid()` |
| 8 | 摇尾巴时屏幕卡住不刷 | `while(WeiBa_Bit)` 无限阻塞，饿死表情任务 | 改成每次只走 20 小步（约 120ms）就返回，让调度器还能跑别的任务 |
| 9 | 呼吸灯节奏变成 8 秒 | TIM3 中断被 2 分频（20ms→40ms），步长没变 | 步长按 40ms 重算，并加编译期整除检查 |
| 10 | **连接/断开蓝牙自己摇 16~27 秒** | 模块吐 `CONNECTED`/`DISCONNECTED`，`S`/`T` 撞上 0x53/0x54 | 20ms 间隙过滤 + 0x52~0x55 只走语音 |
| 11 | **动作执行中表情不切换**（天气表情"粘住"） | 阻塞动作饿死 30ms 表情任务 | 新增 `Face_Service()`，只在表情变了才重画 |
| 12 | `aapt dump badging` 在中文路径下失败 | 工具对非 ASCII 路径的处理问题 | 先复制到 `%TEMP%` 再执行 |
| 13 | `d8` 报 Java 版本过低 | `d8` 需要 Java ≥11，而 javac 侧是 JDK 8 | 分成两套：JDK 8 编译 + JRE 17 跑 d8 |
| 14 | Keil 报 `#1-D: last line of file ends without a newline` | 新增头文件末尾没换行 | 补 `\n`，保持 0 Warning |

---

## 11. 目录结构

```
desktop_pet/
├── Project.uvprojx            Keil 工程（AC5）
├── HardWare/                  板级驱动与应用
│   ├── main.c                 初始化顺序、主循环、TIM3 中断、测试/标定模式
│   ├── Servo.c/.h             四条腿：角度→脉宽、镜像、微调
│   ├── PWM.c/.h               TIM2/TIM3 通道、尾巴、LED
│   ├── PetAction.c/.h         动作实现 + 双通道指令分发
│   ├── Face_Config.c/.h       表情、天气页、电量浮层、Face_Service
│   ├── OLED.c/.h              江协科技 OLED 驱动（软件 I2C）
│   ├── OLED_Data.c/.h         字模 + 7 张全屏位图 + 中文字库
│   ├── BlueTooth.c/.h         USART3、间隙过滤、BlueTooth_Poll
│   ├── Voice.c/.h             USART1 语音
│   ├── Weather.c/.h           天气帧状态机
│   ├── PowerDetection.c/.h    电量换算与滤波
│   ├── AD.c/.h                ADC1 采样
│   ├── Led_Breathing.c/.h     呼吸灯
│   ├── Timer.c/.h             TIM3 初始化
│   ├── Delay.c/.h             SysTick 延时
│   └── Variable.c/.h          全局变量与可调宏
├── TaskScheduler/             上游库 + 移植层
│   ├── TaskScheduler*.h       v4.1.0 源码（header-only）
│   ├── TS_Config.h            特性裁剪
│   ├── TS_Port.c/.h           TIM4 1ms 时基 + 20ms 蓝牙判定
│   └── TS_TaskScheduler.cpp   唯一的库实例化单元
├── User/
│   ├── SchedulerApp.cpp/.h    任务定义与 C 接口
│   ├── stm32f10x_conf.h       标准外设库配置
│   └── stm32f10x_it.c         其余中断
├── Start/                     启动文件与 CMSIS
├── Library/                   标准外设库
├── EIDE/                      VS Code EIDE 工程（eide.yml 需与 uvprojx 同步）
├── AndroidApp/                手机上位机
│   ├── src/.../MainActivity.java
│   ├── res/                   布局、颜色、样式、drawable
│   ├── AndroidManifest.xml
│   ├── build_apk.ps1          不用 Gradle 的构建脚本
│   ├── DeskPetRemote.apk      产物（gitignore）
│   ├── README.md              使用说明
│   └── CHANGELOG.md           更新日志
└── tools/
    └── oled_preview.html      OLED 离线预览（不用烧录就能看天气页排版）
```

---

## 12. 已知限制与后续可做

### 限制

1. **动作是阻塞式的**
   一次摇摆最长 27 秒，这期间任务链上的 `tFace` 完全不跑。目前靠 `Face_Service()`
   和中断里跑的电量/呼吸灯弥补，但本质问题没解决。彻底方案是把 `PetAction.c`
   改写成非阻塞状态机（每个 tick 走一步），之后才能算真正的多任务。

2. **蓝牙指令在 TIM4 中断里分发，与语音中断存在微小竞态**
   语音中断（抢占 1）可以打断 TIM4 中断（抢占 3）里的 `PetAction_Judgmengt(1)`，
   两者同时到达时理论上会混成一条"半语音半蓝牙"的指令。窗口是微秒级，实际影响可忽略。
   彻底解决要把语音也改成"入缓冲 + 轮询"，两个通道走同一条判定路径。

3. **`Face_hello` 位图已无引用**
   数据还在 `OLED_Data.c` 里，删掉可省约 1KB Flash。

4. **`PWM.c` 与 `Servo.c` 有重复的 TIM2 配置**
   `Servo_Init()` 才是真正生效的，`PWM.c` 里那段是历史遗留。合并可减少困惑。

5. **`°` 字形不在 8×16 字库里**
   所以温度只能显示成 `24~29C`，要显示 `℃` 得自己补字模。

### 可做

- 手机端固件升级（BLE OTA）—— 目前只能插线烧
- App 的「显示天气」先联网刷新再显示（现在直接发 `0x51`，显示的是上次同步的数据）
- 指令执行回执（板子回一个字节，App 才能知道"桌宠真的执行了"）
- 电量低时自动趴下/提示
- 加一个 `README.md` 指向本文

---

## 附录：关键常量速查

| 常量 | 值 | 位置 |
| --- | --- | --- |
| `Chongfunumber` | 2 | `Variable.h`（前进/后退/转弯的周期数） |
| `SwingRepeatnumber` | 3 | `Variable.h` |
| `HelloRepeatnumber` | 4 | `Variable.h` |
| `ACTION_TAIL_DELAY_MS` | 2000 | `Variable.h` |
| `SpeedDelay` | 500ms（300~550 可调） | `Variable.c` |
| `SwingDelay` | 6ms/度（3~9 可调） | `Variable.c` |
| `TS_APP_FACE_PERIOD_MS` | 30 | `SchedulerApp.cpp` |
| `PERIODIC_TICK_DIV` | 2 | `main.c` |
| `TS_BT_POLL_DIV` | 20 | `TS_Port.c` |
| `BATTERY_FILTER_SAMPLES` | 100 | `PowerDetection.c` |
| `WEATHER_PAYLOAD` | 4 | `Weather.c` |
| `SERVO_TEST_MODE` | 0 | `main.c` |