#ifndef MOTION_PROFILE_H
#define MOTION_PROFILE_H

typedef struct {
    float start_revs, end_revs, direction;
    float peak_rpm, ramp_s, cruise_s, duration_s;
} MotionProfile;

/* Rest-to-rest S curve: cubic speed ramps, continuous acceleration. */
void MotionProfile_Init(MotionProfile *p, float start, float end,
                        float max_rpm, float max_accel, float max_jerk);
void MotionProfile_Sample(const MotionProfile *p, float time_s,
                          float *position_revs, float *speed_rpm);
#endif
