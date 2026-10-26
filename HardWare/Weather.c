#include "Weather.h"
#include "Variable.h"

#define WEATHER_HEAD1       0xAA
#define WEATHER_HEAD2       0x55
#define WEATHER_PAYLOAD     5

/* 解析状态机 */
#define WX_IDLE             0
#define WX_HEAD2            1
#define WX_LEN              2
#define WX_PAYLOAD          3
#define WX_CHECK            4

static uint8_t Wx_State = WX_IDLE;
static uint8_t Wx_Len   = 0;
static uint8_t Wx_Idx   = 0;
static uint8_t Wx_Buf[8];

/* 天气类型 -> 中文描述。新增类型时同步修改这里，
   并确认字模库 OLED_CF16x16 里包含所用汉字。*/
static const char* const Weather_Text[] =
{
	"晴",      /* 0 */
	"多云",    /* 1 */
	"阴",      /* 2 */
	"小雨",    /* 3 */
	"中雨",    /* 4 */
	"大雨",    /* 5 */
	"阵雨",    /* 6 */
	"雷阵雨",  /* 7 */
	"雪",      /* 8 */
	"雾"       /* 9 */
};

const char* Weather_GetText(void)
{
	if(Weather_Code < (sizeof(Weather_Text) / sizeof(Weather_Text[0])))
	{
		return Weather_Text[Weather_Code];
	}
	return "晴";
}

uint8_t Weather_OnByte(uint8_t b)
{
	uint8_t x;
	uint8_t i;

	switch(Wx_State)
	{
		case WX_IDLE:
			if(b == WEATHER_HEAD1)
			{
				Wx_State = WX_HEAD2;
				return 1;
			}
			return 0;

		case WX_HEAD2:
			if(b == WEATHER_HEAD2)
			{
				Wx_State = WX_LEN;
				return 1;
			}
			/* 第二个字节不对，回到空闲。若它本身又是 0xAA 则继续等 0x55 */
			Wx_State = WX_IDLE;
			if(b == WEATHER_HEAD1)
			{
				Wx_State = WX_HEAD2;
				return 1;
			}
			return 0;

		case WX_LEN:
			/* 长度非法直接丢弃整帧，避免死等 */
			if((b == 0) || (b > sizeof(Wx_Buf)))
			{
				Wx_State = WX_IDLE;
				return 1;
			}
			Wx_Len = b;
			Wx_Idx = 0;
			Wx_State = WX_PAYLOAD;
			return 1;

		case WX_PAYLOAD:
			Wx_Buf[Wx_Idx] = b;
			Wx_Idx++;
			if(Wx_Idx >= Wx_Len)
			{
				Wx_State = WX_CHECK;
			}
			return 1;

		case WX_CHECK:
			x = Wx_Len;
			for(i = 0; i < Wx_Len; i++)
			{
				x ^= Wx_Buf[i];
			}
			/* 校验通过且长度正好是 5 才采用。
			   长度不对的帧（比如旧版只有 TEMP 的 3 字节帧）在上面的状态机里
			   已经被完整吃掉，不会漏到指令通道去，这里只是不采信而已。 */
			if((x == b) && (Wx_Len == WEATHER_PAYLOAD))
			{
				Weather_Code  = Wx_Buf[0];
				Weather_Tmin  = (int8_t)((int16_t)Wx_Buf[1] - 40);
				Weather_Tmax  = (int8_t)((int16_t)Wx_Buf[2] - 40);
				Weather_Hum   = Wx_Buf[3];
				Weather_Valid = 1;
			}
			Wx_State = WX_IDLE;
			return 1;

		default:
			Wx_State = WX_IDLE;
			return 0;
	}
}
