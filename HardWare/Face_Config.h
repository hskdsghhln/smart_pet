#ifndef __FACE_CONFIG_
#define __FACE_CONFIG_

void Face_Config(void);

/* 【供阻塞动作调用】只在表情真的变了的时候重画一次。
   PetAction 里的动作（摇摆 / 走路 / 打招呼…）都是阻塞式的，
   一进去就几十毫秒到几十秒不返回，30ms 的表情任务被饿死，
   表情会停在动作开始前那一帧。动作循环里插一句 Face_Service()
   就能让新表情在 10ms 内显示出来。 */
void Face_Service(void);

#endif
