/*在这里定义所有的变量，并允许外部引用*/
#include "stm32f10x.h"                  // Device header
#include "Variable.h"


/*动作相关*/
uint16_t PAnumbers=Chongfunumber;//动作重复次数
uint16_t TiaoTurn=0;             //向前跳
uint16_t TiaoTurn2=0;            //向后跳
uint16_t Action_Mode=2;   //动作模式,上电后是站立状态
/* 运动速度：SG90 标称 0.3 秒/60 度 => 5ms/度，走完一个 90 度的摆腿需要 450ms。
   取 500ms 给舵机留 50ms 的稳定余量。设太小（比如 200ms）舵机还没转到位下一条
   命令就来了，步态会退化成小幅抖动。可通过命令 0x38 在 300~550ms 之间循环调节。 */
uint16_t SpeedDelay=500;  //运动速度（每个摆腿动作的时长，ms）
uint16_t SwingDelay=6;    //摇摆速度（每 1 度的时间，ms；6ms/度≈标称速度）
uint16_t SwingSeconds=0;  //摇摆时长（秒），0=按 SwingRepeatnumber 固定次数
uint16_t Face_Mode=5;     //表情切换，上电是两个眼睛表情
uint8_t WeiBa_Bit=0;      //摇尾巴判断
uint8_t WeiBa_Value=90;   //摇尾巴的compare值
int8_t WeiBa_Dir=1;       //摇尾巴方向判断
uint16_t Sustainedmove=0; //持续运动
uint16_t RxData_Voice=0;        //语音串口接收数据变量
uint16_t RxData_Blue=0;   //蓝牙串口接收数据变量


/*呼吸灯相关*/
uint16_t Time;            //呼吸灯间隔时间
uint16_t HuXi;            //呼吸灯输出脉冲数
uint16_t PanDuan=1;       //呼吸灯模式
uint16_t Wait=0;          //间隔时间
uint16_t AllLed=1;        //开启灯光
uint16_t BreatheLed=0;    //开启呼吸灯

/*测电量相关*/
float Battery_Value=0;
uint16_t temp;            //每次测得的当前电量（单个样本）
uint32_t temp1=0;         //累加器：滤波窗口内所有样本之和（必须 32 位，见 PowerDetection.c）
uint16_t CurBattery;      //当前电量（滤波后的平均值）
uint8_t Battery_num=0;    //获取电量滤波系数
uint8_t Battery_Bit=1;

/*天气相关*/
uint8_t Weather_Valid=0;         //1=已有有效天气数据
uint8_t Weather_Code=0;          //天气类型 0..9
int8_t  Weather_Tmin=0;          //当天最低温（℃）
int8_t  Weather_Tmax=0;          //当天最高温（℃）
uint8_t Weather_Hum=0;           //湿度（%）
