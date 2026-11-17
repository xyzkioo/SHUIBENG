/**
  ******************************************************************************
  * @file    pid.c
  * @brief   最简单的 PID（位置式）
  ******************************************************************************
  *   u = Kp*e + Ki*Σe + Kd*(e - e_last)
  ******************************************************************************
  */

#include "pid.h"

void PID_Init(PID_t *p, float kp, float ki, float kd, float out_max)
{
    if (p == 0)
    {
        return;
    }

    p->Kp       = kp;
    p->Ki       = ki;
    p->Kd       = kd;
    p->target   = 0.0f;
    p->actual   = 0.0f;
    p->err      = 0.0f;
    p->err_last = 0.0f;
    p->integral = 0.0f;
    p->out      = 0.0f;
    p->out_max  = out_max;
}

float PID_Calc(PID_t *p, float target, float actual)
{
    return PID_CalcWithBias(p, target, actual, 0.0f);
}

float PID_CalcWithBias(PID_t *p, float target, float actual, float bias)
{
    if (p == 0) return 0.0f;
    return PID_CalcWithLimits(p, target, actual, bias, -p->out_max, p->out_max);
}

float PID_CalcWithLimits(PID_t *p, float target, float actual, float bias,
                         float out_min, float out_max)
{
    float i_max;
    float previous_integral;
    float pd;
    float candidate;

    if (p == 0)
    {
        return 0.0f;
    }

    if (out_min < -p->out_max) out_min = -p->out_max;
    if (out_max > p->out_max) out_max = p->out_max;
    if (out_min > out_max) return 0.0f;

    p->target = target;
    p->actual = actual;

    /* 1. 本次误差 */
    p->err = target - actual;

    /* 2. 误差累加（积分项），并做限幅，防止积分饱和
     *    限到 out_max/Ki：这样积分项单独最多就顶到输出上限，
     *    电机堵转或目标突变时不会"越积越多"导致回不来 */
    previous_integral = p->integral;
    p->integral += p->err;
    if (p->Ki > 0.0f)
    {
        i_max = p->out_max / p->Ki;
        if (p->integral > i_max)
        {
            p->integral = i_max;
        }
        else if (p->integral < -i_max)
        {
            p->integral = -i_max;
        }
    }
    else
    {
        p->integral = 0.0f;    /* Ki = 0 时不积分 */
        previous_integral = 0.0f;
    }

    /* 包含前馈的总输出一起限幅；饱和时只允许积分向解饱和方向变化。 */
    pd = p->Kp * p->err + p->Kd * (p->err - p->err_last);
    candidate = bias + pd + p->Ki * p->integral;
    if (((candidate > out_max) && (p->err > 0.0f)) ||
        ((candidate < out_min) && (p->err < 0.0f)))
    {
        p->integral = previous_integral;
    }
    p->out = bias + pd + p->Ki * p->integral;

    /* 4. 输出限幅 */
    if (p->out > out_max)
    {
        p->out = out_max;
    }
    else if (p->out < out_min)
    {
        p->out = out_min;
    }

    /* 5. 保存本次误差，下次算微分用 */
    p->err_last = p->err;

    return p->out;
}

void PID_Reset(PID_t *p)
{
    if (p == 0)
    {
        return;
    }

    p->err      = 0.0f;
    p->err_last = 0.0f;
    p->integral = 0.0f;
    p->out      = 0.0f;
}
