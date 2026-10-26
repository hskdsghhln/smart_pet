#include "stm32f10x.h"      
#include "OLED.h"
#include "BlueTooth.h"
#include "Variable.h"
#include "Weather.h"

/* 把整数追加到字符串末尾，返回新的长度。
   OLED_ShowNum 会按固定长度取位、高位补零（5 显示成 "05"，100 按 2 位显示成 "00"），
   天气这里数值长短不定，所以自己转字符，按实际位数输出。 */
static uint8_t Face_NumAppend(char *dst, uint8_t n, int16_t v)
{
	uint16_t u;
	if(v < 0)
	{
		dst[n++] = '-';
		u = (uint16_t)(-v);
	}
	else
	{
		u = (uint16_t)v;
	}
	if(u >= 10)
	{
		dst[n++] = (char)('0' + (u / 10) % 10);
	}
	dst[n++] = (char)('0' + (u % 10));
	return n;
}

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
				char buf[12];
				uint8_t len;
				while(txt[n*3] != '\0')
				{
					n++;
				}
				/*天气描述居中*/
				OLED_ShowChinese((int16_t)((128 - n*16)/2), 2, (char*)txt);

				/*温度区间，左对齐于 x=48，形如 24~29C，最宽 -15~-8C 共 7 字符(56px)*/
				OLED_ShowChinese(8, 24, "温度");
				len = 0;
				len = Face_NumAppend(buf, len, (int16_t)Weather_Tmin);
				buf[len++] = '~';
				len = Face_NumAppend(buf, len, (int16_t)Weather_Tmax);
				buf[len++] = 'C';
				buf[len] = '\0';
				OLED_ShowString(48, 24, buf, OLED_8X16);

				/*湿度，同样左对齐于 x=48*/
				OLED_ShowChinese(8, 46, "湿度");
				len = 0;
				len = Face_NumAppend(buf, len, (int16_t)Weather_Hum);
				buf[len++] = '%';
				buf[len] = '\0';
				OLED_ShowString(48, 46, buf, OLED_8X16);
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
