#include "pump_app.h"
#include "motion_profile.h"
#include <math.h>
#include <string.h>

#define KEY_DEBOUNCE_MS 40U
#define AXIS_MOVE_TIMEOUT_MS 15000U
#define AXIS_STALL_TIMEOUT_MS 2000U

typedef struct {
    uint8_t raw, stable, armed;
    uint32_t changed_at, pressed_at;
} Key;

static PumpAppStatus status;
static Key keys[2];
static uint16_t previous_encoder;
static int8_t sign;
static int8_t move_direction;
static uint8_t suppress_key1_release;
static uint32_t move_started, last_motion;
static MotionProfile profile;
static float correction_rpm;
static uint32_t last_control;
static int32_t move_start_counts;
static float configured_speed, configured_step;

static float Clamp(float value, float limit)
{
    if (value > limit) return limit;
    if (value < -limit) return -limit;
    return value;
}

static void Stop(PumpAxisState state)
{
    status.axis_state = state;
    status.speed_target_rpm = 0.0f;
    status.profile_speed_rpm = 0.0f;
    correction_rpm = 0.0f;
}

/* Returns 1=debounced press, 2=debounced release. Boot-held keys are ignored. */
static uint8_t KeyUpdate(Key *key, uint8_t down, uint32_t now)
{
    if (key->raw != down) {
        key->raw = down;
        key->changed_at = now;
    }
    if ((now - key->changed_at) < KEY_DEBOUNCE_MS) return 0U;
    if (!key->armed) {
        key->stable = down;
        if (!down) key->armed = 1U;
        return 0U;
    }
    if (key->stable == down) return 0U;
    key->stable = down;
    if (down) {
        key->pressed_at = now;
        return 1U;
    }
    return 2U;
}

static void Move(float revs, uint32_t now)
{
    float current = status.position_counts / PUMP_ENCODER_COUNTS_PER_REV;
    status.target_revs = current + revs;
#if PUMP_AXIS_USE_SOFT_LIMITS
    /* An out-of-range position requires homing, never a reverse move to the limit. */
    if (current < -PUMP_AXIS_TRAVEL_REVS || current > PUMP_AXIS_TRAVEL_REVS) {
        status.target_revs = current;
        Stop(PUMP_AXIS_LIMIT_FAULT);
        return;
    }
    status.target_revs = Clamp(status.target_revs, PUMP_AXIS_TRAVEL_REVS);
#endif
    move_direction = status.target_revs > current ? 1 : -1;
    move_started = last_motion = now;
    move_start_counts = status.position_counts;
    last_control = now;
    correction_rpm = 0.0f;
    MotionProfile_Init(&profile, current, status.target_revs,
                       configured_speed, PUMP_AXIS_MAX_ACCEL, PUMP_AXIS_MAX_JERK);
    status.reference_revs = current;
    status.profile_speed_rpm = status.speed_target_rpm = 0.0f;
    status.profile_duration_s = profile.duration_s;
    status.axis_state = PUMP_AXIS_MOVING;
}

void PumpApp_Init(uint32_t now, uint16_t encoder, int8_t encoder_sign)
{
    memset(&status, 0, sizeof(status));
    memset(keys, 0, sizeof(keys));
    keys[0].changed_at = keys[1].changed_at = now;
    previous_encoder = encoder;
    sign = encoder_sign < 0 ? -1 : 1;
    suppress_key1_release = 0U;
    correction_rpm = 0.0f;
    configured_speed = PUMP_AXIS_MAX_RPM;
    configured_step = PUMP_AXIS_STEP_REVS;
}

void PumpApp_Poll(uint32_t now, uint16_t encoder, uint8_t key1_down,
                  uint8_t key2_down)
{
    int32_t delta = (int32_t)(int16_t)(uint16_t)(encoder - previous_encoder) * sign;
    uint8_t event1, event2;
    float error;
    previous_encoder = encoder;
    status.position_counts += delta;
    if (delta != 0) last_motion = now;

    event1 = KeyUpdate(&keys[0], key1_down, now);
    event2 = KeyUpdate(&keys[1], key2_down, now);
    status.key1_down = keys[0].stable;
    status.key2_down = keys[1].stable;
    if (event1 == 1U) {
        suppress_key1_release = status.axis_state == PUMP_AXIS_MOVING;
        if (suppress_key1_release) {
            status.last_key1_action = 3U;
            Stop(PUMP_AXIS_IDLE);
        }
    }
    if ((event1 == 2U) && !suppress_key1_release) {
        status.key1_press_ms = now - keys[0].pressed_at;
        status.last_key1_action = status.key1_press_ms >= PUMP_KEY_LONG_MS ? 2U : 1U;
        Move(status.last_key1_action == 2U ?
             -PUMP_AXIS_EXTEND_SIGN * configured_step :
              PUMP_AXIS_EXTEND_SIGN * configured_step, now);
    }
    if (event2 == 1U) {
        status.pump_pwm = status.pump_pwm == 0U ? PUMP_RUN_PWM : 0U;
    }

    if (status.axis_state != PUMP_AXIS_MOVING) return;
    /* A sign/wiring mismatch must not increase position error until timeout. */
    if ((status.position_counts - move_start_counts) * move_direction <=
        -PUMP_AXIS_WRONG_DIRECTION_COUNTS) {
        Stop(PUMP_AXIS_DIRECTION_FAULT);
        return;
    }
    error = status.target_revs * PUMP_ENCODER_COUNTS_PER_REV - status.position_counts;
    /* Normal arrival is near zero profile speed; crossing also stops for safety. */
    if (error * move_direction <= PUMP_AXIS_TOLERANCE_COUNTS) {
        Stop(PUMP_AXIS_AT_TARGET);
    } else if ((now - move_started >= AXIS_MOVE_TIMEOUT_MS) ||
               (now - last_motion >= AXIS_STALL_TIMEOUT_MS)) {
        Stop(PUMP_AXIS_TIMEOUT);
    } else {
        uint32_t elapsed = now - last_control;
        if (elapsed >= 20U) {
            float correction;
            float forward;
            float braking_limit;
            float minimum;
            float rise = (now - move_started) / (float)PUMP_AXIS_MIN_RAMP_MS;
            MotionProfile_Sample(&profile, (now - move_started) * 0.001f,
                                  &status.reference_revs, &status.profile_speed_rpm);
            if (fabsf(status.reference_revs - status.position_counts /
                      PUMP_ENCODER_COUNTS_PER_REV) > PUMP_AXIS_MAX_TRACKING_ERROR_REVS) {
                Stop(PUMP_AXIS_TRACKING_FAULT);
                return;
            }
            correction = Clamp(PUMP_AXIS_POSITION_GAIN * (status.reference_revs -
                         status.position_counts / PUMP_ENCODER_COUNTS_PER_REV),
                         PUMP_AXIS_CORRECTION_MAX_RPM);
            /* Smooth quantized encoder position correction with a 250ms filter. */
            correction_rpm += (elapsed / (250.0f + elapsed)) * (correction - correction_rpm);
            forward = move_direction * (status.profile_speed_rpm + correction_rpm);
            if (rise > 1.0f) rise = 1.0f;
            minimum = PUMP_AXIS_MIN_RPM * rise * rise * (3.0f - 2.0f * rise);
            if (forward < minimum) forward = minimum;
            if (forward > configured_speed) forward = configured_speed;
            /* Use real remaining travel to brake even when tracking leads the plan.
             * v(RPM)^2 = 120 * a(RPM/s) * distance(rev). Reserve the stop window
             * and use half the planned acceleration as a conservative envelope. */
            braking_limit = sqrtf(60.0f * PUMP_AXIS_MAX_ACCEL *
                (error * move_direction - PUMP_AXIS_TOLERANCE_COUNTS) /
                PUMP_ENCODER_COUNTS_PER_REV);
            /* Final approach is intentionally nonzero creep, then arrival brakes. */
            if (braking_limit < minimum) braking_limit = minimum;
            if (forward > braking_limit) forward = braking_limit;
            status.speed_target_rpm = move_direction * forward;
            last_control = now;
        }
    }
}

const PumpAppStatus *PumpApp_GetStatus(void)
{
    return &status;
}

int PumpApp_StartMove(uint32_t now, int retract)
{
    if (status.axis_state == PUMP_AXIS_MOVING || keys[0].raw || keys[0].stable) return 0;
    status.last_key1_action = retract ? 2U : 1U;
    Move((retract ? -PUMP_AXIS_EXTEND_SIGN : PUMP_AXIS_EXTEND_SIGN) * configured_step, now);
    return status.axis_state == PUMP_AXIS_MOVING;
}

void PumpApp_StopAxis(void)
{
    suppress_key1_release = 1U; /* A held physical key must not restart on release. */
    Stop(PUMP_AXIS_IDLE);
}

void PumpApp_StopAll(void)
{
    PumpApp_StopAxis();
    status.pump_pwm = 0U;
}

int PumpApp_SetPump(uint16_t pwm)
{
    if (pwm > 1000U) return 0;
    status.pump_pwm = pwm;
    return 1;
}

int PumpApp_SetSpeed(float rpm)
{
    if (status.axis_state == PUMP_AXIS_MOVING || !isfinite(rpm) ||
        rpm < PUMP_AXIS_MIN_RPM || rpm > PUMP_AXIS_MAX_RPM) return 0;
    configured_speed = rpm;
    return 1;
}

int PumpApp_SetStep(float revs)
{
    if (status.axis_state == PUMP_AXIS_MOVING || !isfinite(revs) ||
        revs < 0.1f || revs > 10.0f) return 0;
    configured_step = revs;
    return 1;
}

float PumpApp_GetSpeed(void) { return configured_speed; }
float PumpApp_GetStep(void) { return configured_step; }

