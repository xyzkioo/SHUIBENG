#include "motion_profile.h"
#include <math.h>

static float RampTime(float rpm, float accel, float jerk)
{
    float ta = 1.5f * rpm / accel;
    float tj = sqrtf(6.0f * rpm / jerk);
    return ta > tj ? ta : tj;
}

void MotionProfile_Init(MotionProfile *p, float start, float end,
                        float max_rpm, float max_accel, float max_jerk)
{
    float distance = fabsf(end - start);
    float peak = max_rpm;
    float low = 0.0f, high = max_rpm;
    p->start_revs = start;
    p->end_revs = end;
    p->direction = end >= start ? 1.0f : -1.0f;
    p->peak_rpm = p->ramp_s = p->cruise_s = p->duration_s = 0.0f;
    if (distance <= 0.0f || max_rpm <= 0.0f || max_accel <= 0.0f || max_jerk <= 0.0f) return;
    /* Two symmetric ramps cover peak_rpm * ramp_s / 60 revolutions. */
    if (peak * RampTime(peak, max_accel, max_jerk) / 60.0f > distance) {
        for (unsigned i = 0; i < 24; ++i) {
            peak = 0.5f * (low + high);
            if (peak * RampTime(peak, max_accel, max_jerk) / 60.0f > distance) high = peak;
            else low = peak;
        }
        peak = low;
    }
    p->peak_rpm = peak;
    p->ramp_s = RampTime(peak, max_accel, max_jerk);
    p->cruise_s = distance * 60.0f / peak - p->ramp_s;
    if (p->cruise_s < 0.0f) p->cruise_s = 0.0f;
    p->duration_s = 2.0f * p->ramp_s + p->cruise_s;
}

void MotionProfile_Sample(const MotionProfile *p, float time_s,
                          float *position_revs, float *speed_rpm)
{
    float distance, speed, u;
    if (time_s <= 0.0f) {
        *position_revs = p->start_revs; *speed_rpm = 0.0f; return;
    }
    if (time_s >= p->duration_s || p->ramp_s <= 0.0f) {
        *position_revs = p->end_revs; *speed_rpm = 0.0f; return;
    }
    if (time_s < p->ramp_s) {
        u = time_s / p->ramp_s;
        speed = p->peak_rpm * u * u * (3.0f - 2.0f * u);
        distance = p->peak_rpm * p->ramp_s / 60.0f * u * u * u * (1.0f - 0.5f * u);
    } else if (time_s < p->ramp_s + p->cruise_s) {
        speed = p->peak_rpm;
        distance = p->peak_rpm / 60.0f * (time_s - 0.5f * p->ramp_s);
    } else {
        u = (p->duration_s - time_s) / p->ramp_s;
        speed = p->peak_rpm * u * u * (3.0f - 2.0f * u);
        distance = fabsf(p->end_revs - p->start_revs) -
                   p->peak_rpm * p->ramp_s / 60.0f * u * u * u * (1.0f - 0.5f * u);
    }
    *position_revs = p->start_revs + p->direction * distance;
    *speed_rpm = p->direction * speed;
}
