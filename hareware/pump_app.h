#ifndef PUMP_APP_H
#define PUMP_APP_H

#include <stdint.h>

/* Current STM32 board: 11 PPR, quadrature x4, gearbox 4.4. */
#define PUMP_ENCODER_COUNTS_PER_REV (11.0f * 4.0f * 4.4f)
#define PUMP_AXIS_STEP_REVS 5.0f
#define PUMP_AXIS_EXTEND_SIGN (1.0f) /* EXTEND/short press: positive; RETRACT/long press: negative. */
#define PUMP_AXIS_TRAVEL_REVS 10.0f
#define PUMP_AXIS_USE_SOFT_LIMITS 0 /* No homing datum: moves are relative to current position. */
#define PUMP_AXIS_MAX_RPM 200.0f
#define PUMP_AXIS_MIN_RPM 60.0f /* Creep until arrival; not a guaranteed measured speed. */
#define PUMP_AXIS_MIN_RAMP_MS 300U
#define PUMP_AXIS_MAX_PWM 250.0f /* Axis only: cap drive at 25% duty. */
#define PUMP_KEY_LONG_MS 800U
#define PUMP_AXIS_MAX_ACCEL 250.0f /* RPM/s, planned S curve */
#define PUMP_AXIS_MAX_JERK 800.0f  /* RPM/s^2, planned S curve */
#define PUMP_AXIS_CORRECTION_MAX_RPM 30.0f
#define PUMP_SPEED_KP 4.0f
#define PUMP_SPEED_KI 0.8f /* Per 50ms sample, error in encoder counts/sample. */
#define PUMP_AXIS_POSITION_GAIN 120.0f /* RPM per remaining output-shaft rev */
#define PUMP_AXIS_TOLERANCE_COUNTS 3.0f
#define PUMP_AXIS_WRONG_DIRECTION_COUNTS 12 /* Stop after ~0.062 rev opposite command. */
#define PUMP_AXIS_MAX_TRACKING_ERROR_REVS 1.0f
#define PUMP_RUN_PWM 800U

typedef enum {
    PUMP_AXIS_IDLE,
    PUMP_AXIS_MOVING,
    PUMP_AXIS_AT_TARGET,
    PUMP_AXIS_TIMEOUT,
    PUMP_AXIS_DIRECTION_FAULT,
    PUMP_AXIS_TRACKING_FAULT,
    PUMP_AXIS_LIMIT_FAULT
} PumpAxisState;

typedef struct {
    int32_t position_counts;
    float target_revs;
    float speed_target_rpm;
    float reference_revs;
    float profile_speed_rpm;
    float profile_duration_s;
    PumpAxisState axis_state;
    uint16_t pump_pwm;
    uint8_t key1_down;
    uint8_t key2_down;
    uint8_t last_key1_action; /* 1=extend, 2=retract, 3=stop while moving */
    uint32_t key1_press_ms;
} PumpAppStatus;

/* Raw TIM2 counter; encoder_sign normalizes forward rotation to positive. */
void PumpApp_Init(uint32_t now, uint16_t encoder, int8_t encoder_sign);
void PumpApp_Poll(uint32_t now, uint16_t encoder, uint8_t key1_down,
                  uint8_t key2_down);
const PumpAppStatus *PumpApp_GetStatus(void);
int PumpApp_StartMove(uint32_t now, int retract);
void PumpApp_StopAxis(void);
void PumpApp_StopAll(void);
int PumpApp_SetPump(uint16_t pwm);
int PumpApp_SetSpeed(float rpm);
int PumpApp_SetStep(float revs);
float PumpApp_GetSpeed(void);
float PumpApp_GetStep(void);

#endif
