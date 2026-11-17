/**
  ******************************************************************************
  * @file    pid.h
  * @brief   PID（位置式，适合电机速度环 / 位置环）
  ******************************************************************************
  * 位置式公式（每算一次就得到一次完整的输出）：
  *
  *   u = Kp * e          +  Ki * Σe            +  Kd * (e - e_last)
  *       └─比例：误差多大   └─积分：误差累积      └─微分：误差变化快慢
  *         就用力多大         抵掉稳态误差          提前刹车，抑制超调
  *
  *   e = 目标值 - 实际值
  *
  * 用法：
  *   PID_t pid;
  *   PID_Init(&pid, 1.0f, 0.05f, 0.0f, 1000.0f);   // Kp Ki Kd 输出限幅
  *   每 50ms 调一次：
  *   u = PID_Calc(&pid, 目标值, 实际值);           // u 就是本次输出
  ******************************************************************************
  */

#ifndef __PID_H__
#define __PID_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float Kp;        /* 比例系数：误差乘多少倍 */
    float Ki;        /* 积分系数：误差累加值乘多少倍 */
    float Kd;        /* 微分系数：误差变化量乘多少倍 */

    float target;    /* 目标值 */
    float actual;    /* 实际值 */
    float err;       /* 本次误差   e[k]   */
    float err_last;  /* 上次误差   e[k-1] ，微分项要用 */
    float integral;  /* 误差累加值 Σe，已经做了限幅 */
    float out;       /* 本次输出 */
    float out_max;   /* 输出限幅：-out_max ~ +out_max */
} PID_t;

/**
 * @brief  PID 初始化
 * @param  p         PID 结构体
 * @param  kp/ki/kd  PID 三个参数
 * @param  out_max   输出限幅绝对值（例如 PWM 满量程 1000）
 */
void PID_Init(PID_t *p, float kp, float ki, float kd, float out_max);

/**
 * @brief  位置式 PID 计算
 * @param  p        PID 结构体
 * @param  target   目标值
 * @param  actual   实际值（编码器测出来的速度）
 * @retval 限幅后的输出（有符号）
 * @note   内部已做积分限幅，不需要再单独抗积分饱和
 */
float PID_Calc(PID_t *p, float target, float actual);

/** @brief 加入输出前馈；bias 与 PWM 输出同单位，总输出共同限幅并防积分饱和。 */
float PID_CalcWithBias(PID_t *p, float target, float actual, float bias);

/* Output bounds participate in anti-windup (e.g. extension permits only 0..max). */
float PID_CalcWithLimits(PID_t *p, float target, float actual, float bias,
                         float out_min, float out_max);

/** @brief 清空误差与积分，输出归零（换向、重新使能时调用） */
void PID_Reset(PID_t *p);

#ifdef __cplusplus
}
#endif

#endif /* __PID_H__ */
