#ifndef __BLUE_TOOTH_H
#define __BLUE_TOOTH_H

#include "stm32f10x.h"

void BlueTooth_Init(void);

/* 向手机发一个字节：PB10(USART3_TX) -> 蓝牙模块 -> 手机收到 FFF1 通知。
   目前只用于"请求同步天气"，见 Weather.h 的 WEATHER_REQ_SYNC。 */
void BlueTooth_SendByte(uint8_t b);

#endif
