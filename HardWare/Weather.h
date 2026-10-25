#ifndef __WEATHER_H_
#define __WEATHER_H_

#include "stm32f10x.h"

/*----------------------------------------------------------------------------
  天气帧（手机 --蓝牙--> STM32）

  格式：  AA 55 LEN CODE TEMP HUM XOR
    LEN  = 3（后面跟着 3 个数据字节）
    CODE = 天气类型 0..9，见 Weather_GetText()
    TEMP = 温度 + 40（0x00 表示 -40℃，0x64 表示 +60℃）
    HUM  = 湿度 0..100（%）
    XOR  = LEN ^ CODE ^ TEMP ^ HUM

  为什么不用纯文本：单字节指令通道（0x29~0x51）按字节立刻执行，
  多字节文本里的每个字节都会被当成一条命令。所以天气必须走带帧头的
  独立通道，由本模块先把整帧收完再解析。
----------------------------------------------------------------------------*/

/* 把蓝牙收到的 1 个字节喂给解析器
   返回 1 = 这个字节属于天气帧，调用方不要再当命令处理
   返回 0 = 不是天气帧的一部分，调用方照常执行 PetAction_Judgmengt() */
uint8_t Weather_OnByte(uint8_t b);

/* 取当前天气类型对应的中文描述（UTF-8，可直接传给 OLED_ShowChinese） */
const char* Weather_GetText(void);

#endif
