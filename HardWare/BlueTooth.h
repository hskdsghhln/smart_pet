#ifndef __BLUE_TOOTH_H
#define __BLUE_TOOTH_H

#include "stm32f10x.h"

void BlueTooth_Init(void);

/* 向手机发一个字节：PB10(USART3_TX) -> 蓝牙模块 -> 手机收到 FFF1 通知。
   目前只用于"请求同步天气"，见 Weather.h 的 WEATHER_REQ_SYNC。 */
void BlueTooth_SendByte(uint8_t b);

/* 单字节指令的判定与分发，每 20ms 调用一次（由 TIM4 中断分频驱动）。
   一次收到多个字节的"簇"会被整簇丢弃，用来过滤模块连接/断开时吐的
   AT 状态字符串，详见 BlueTooth.c 里的说明。 */
void BlueTooth_Poll(void);

#endif
