#ifndef __SERVO_H
#define __SERVO_H

#include <stdint.h>

void Servo_Init(void);
void Servo_Angle1(float Angle);
void Servo_Angle2(float Angle);
void Servo_Angle3(float Angle);
void Servo_Angle4(float Angle);

/*中位微调：单位是度，正=往角度大的方向偏、负=往角度小的方向偏*/
void  Servo_SetTrim(uint8_t Ch, int8_t Deg);
int8_t Servo_GetTrim(uint8_t Ch);

#endif
