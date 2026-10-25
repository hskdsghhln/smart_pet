#include "stm32f10x.h"      
#include "OLED.h"
#include "BlueTooth.h"
#include "Variable.h"
#include "Weather.h"

//实现表情变化，调节是进中断后
void Face_Config(void)
{
	/*图案处理*/
	switch(Face_Mode)
	{
		case 0:
	    OLED_Clear();
	    OLED_ShowImage(0,0,128,64,Face_sleep);//睡觉
		  break;
		case 1:
			OLED_Clear();
		  OLED_ShowImage(0,0,128,64,Face_stare);//瞪大眼
		  break;
		case 2:
			OLED_Clear();
	  	OLED_ShowImage(0,0,128,64,Face_happy);//快乐
		  break;
	  case 3:
			OLED_Clear();
	  	OLED_ShowImage(0,0,128,64,Face_mania);//狂热
		  break;
		case 4:
			OLED_Clear();
	  	OLED_ShowImage(0,0,128,64,Face_very_happy);//非常快乐
		  break;
		case 5:
			OLED_Clear();
	  	OLED_ShowImage(0,0,128,64,Face_eyes);//眼睛
		 break;
		case 6:
			OLED_Clear();
	  	/*【按用户要求】原来的 Face_hello（椭圆眼 + 横条 + ω 嘴）不再显示，
	  	   打招呼 / 伸懒腰 / 拉伸统一改成 Face_eyes（两个大眼睛）。
	  	   位图数据暂时保留在 OLED_Data.c 里没有删除，确认效果没问题后再删可省 1KB。*/
	  	OLED_ShowImage(0,0,128,64,Face_eyes);//眼睛（替代原来的打招呼表情）
			break;
		case 7:
		{
			/*【天气表情】数据由手机经蓝牙下发（AA 55 帧），语音或蓝牙发送 0x51 切到这里*/
			OLED_Clear();
			if(!Weather_Valid)
			{
				OLED_ShowString(40,24,"NO DATA",OLED_8X16);
			}
			else
			{
				const char* txt = Weather_GetText();
				uint8_t n = 0;
				while(txt[n*3] != '\0')
				{
					n++;
				}
				/*天气描述居中*/
				OLED_ShowChinese((int16_t)((128 - n*16)/2), 4, (char*)txt);

				/*温度*/
				OLED_ShowChinese(8, 26, "温度");
				if(Weather_Temp < 0)
				{
					OLED_ShowChar(48, 26, '-', OLED_8X16);
					OLED_ShowNum(56, 26, (uint16_t)(-(int16_t)Weather_Temp), 2, OLED_8X16);
					OLED_ShowString(72, 26, "C", OLED_8X16);
				}
				else
				{
					OLED_ShowNum(48, 26, (uint16_t)Weather_Temp, 2, OLED_8X16);
					OLED_ShowString(64, 26, "C", OLED_8X16);
				}

				/*湿度*/
				OLED_ShowChinese(8, 44, "湿度");
				OLED_ShowNum(48, 44, Weather_Hum, 2, OLED_8X16);
				OLED_ShowString(64, 44, "%", OLED_8X16);
			}
			break;
		}
	}
	
	/*电量处理*/
	if(Battery_Bit && (Face_Mode != 7))
	{
		OLED_ShowString(0,0,"Power:",OLED_6X8);
		if(CurBattery>=110) OLED_ShowString(36,0,"Charging",OLED_6X8);
		else if(CurBattery>=100)OLED_ShowNum(36,0,CurBattery,3,OLED_6X8);
		else OLED_ShowNum(36,0,CurBattery,2,OLED_6X8);
	}
	

//		OLED_Clear();

//    OLED_Printf(0,0,OLED_6X8,"Mode:%d",Action_Mode);	
//		OLED_Printf(80,0,OLED_6X8,"RX:%X",RxData_Voice);
//		

	/*显示图案*/
		OLED_Update();
}
