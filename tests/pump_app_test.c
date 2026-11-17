#include "pump_app.h"
#include "motion_profile.h"
#include "pid.h"
#include "bt_control.h"
#include <stddef.h>
#include <string.h>

/* Freestanding harness: runs the production C module in a Cortex-M emulator. */
void *memset(void *p, int value, size_t size)
{
    unsigned char *out = p;
    while (size--) *out++ = (unsigned char)value;
    return p;
}

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
int *__errno(void) { static int error; return &error; }
static uint32_t tick;
static uint16_t encoder;
static uint8_t *uart_rx;
static char uart_output[4096];
static unsigned uart_length;
uint32_t HAL_GetTick(void) { return tick; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t size)
{ (void)uart; (void)size; uart_rx = data; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart, const uint8_t *data, uint16_t size)
{
    (void)uart;
    for (unsigned i = 0; i < size && uart_length < sizeof(uart_output) - 1; ++i)
        uart_output[uart_length++] = (char)data[i];
    uart_output[uart_length] = 0;
    return HAL_OK;
}
static void Receive(UART_HandleTypeDef *uart, const char *data)
{
    while (*data) { *uart_rx = (uint8_t)*data++; HAL_UART_RxCpltCallback(uart); }
    BT_Control_Task();
}
static void Poll(uint32_t dt, uint8_t k1, uint8_t k2)
{
    tick += dt;
    PumpApp_Poll(tick, encoder, k1, k2);
}
static void Reset(uint32_t start)
{
    tick = start;
    encoder = 0U;
    PumpApp_Init(tick, encoder, -1);
    Poll(41, 0, 0);
}
static void ShortKey1(void)
{
    Poll(1, 1, 0); Poll(40, 1, 0);
    Poll(80, 0, 0); Poll(40, 0, 0);
}

int test_main(void)
{
    const PumpAppStatus *s = PumpApp_GetStatus();
    Reset(0);
    CHECK(s->axis_state == PUMP_AXIS_IDLE && s->pump_pwm == 0);
    /* Bounce must not start a move. */
    Poll(1, 1, 0); Poll(10, 0, 0); Poll(50, 0, 0);
    CHECK(s->axis_state == PUMP_AXIS_IDLE);
    ShortKey1();
    CHECK(s->target_revs == PUMP_AXIS_EXTEND_SIGN * PUMP_AXIS_STEP_REVS && s->speed_target_rpm == 0.0f);
    /* Encoder wraps 0 -> 65535; normalized position goes positive. */
    encoder = 65535; Poll(1, 0, 0);
    CHECK(s->position_counts == 1);
    /* A debounced press stops immediately, and its release must not restart. */
    Poll(1, 1, 0); Poll(40, 1, 0);
    CHECK(s->axis_state == PUMP_AXIS_IDLE && s->speed_target_rpm == 0);
    Poll(500, 1, 0); Poll(1, 0, 0); Poll(40, 0, 0);
    CHECK(s->axis_state == PUMP_AXIS_IDLE);
    /* Pump toggles once, even if held for many scans. */
    Poll(1, 0, 1); Poll(40, 0, 1);
    CHECK(s->pump_pwm == 800);
    for (int i = 0; i < 100; ++i) Poll(20, 0, 1);
    CHECK(s->pump_pwm == 800);
    Poll(1, 0, 0); Poll(40, 0, 0);
    Poll(1, 0, 1); Poll(40, 0, 1);
    CHECK(s->pump_pwm == 0);
    /* Long key1 release retracts in the negative direction. */
    Reset(0);
    Poll(1, 1, 0); Poll(40, 1, 0); Poll(820, 1, 0);
    Poll(1, 0, 0); Poll(40, 0, 0);
    CHECK(s->target_revs == -PUMP_AXIS_EXTEND_SIGN * PUMP_AXIS_STEP_REVS && s->speed_target_rpm == 0.0f);
    /* Slow near target; reaching tolerance stops rather than reverses. */
    encoder = 50; Poll(50, 0, 0);
    CHECK(s->speed_target_rpm <= 0 && s->speed_target_rpm > -100);
    encoder = (uint16_t)(int32_t)(PUMP_AXIS_STEP_REVS * PUMP_ENCODER_COUNTS_PER_REV); Poll(50, 0, 0);
    CHECK(s->axis_state == PUMP_AXIS_AT_TARGET && s->speed_target_rpm == 0);
    ShortKey1();
    CHECK(s->axis_state == PUMP_AXIS_MOVING && s->target_revs >= 0);
    Poll(2001, 0, 0);
    CHECK(s->axis_state == PUMP_AXIS_TIMEOUT && s->speed_target_rpm == 0);
    /* Without homing, never clamp a relative move against an arbitrary boot limit. */
    Reset(0); encoder = 1742; Poll(1, 0, 0); ShortKey1();
    CHECK(s->target_revs < -8.99f + PUMP_AXIS_STEP_REVS && s->target_revs > -9.01f + PUMP_AXIS_STEP_REVS);
    /* Regression: out-of-range position must still move relative, never to -10. */
    Reset(0); encoder = 4946; Poll(1, 0, 0); ShortKey1();
    CHECK(s->target_revs < -25.54f + PUMP_AXIS_STEP_REVS && s->target_revs > -25.56f + PUMP_AXIS_STEP_REVS);
    Poll(20, 0, 0);
    CHECK(s->speed_target_rpm > 0 && s->axis_state == PUMP_AXIS_MOVING);
    encoder = (uint16_t)(4946 - (int32_t)(PUMP_AXIS_STEP_REVS * PUMP_ENCODER_COUNTS_PER_REV)); Poll(20, 0, 0);
    CHECK(s->axis_state == PUMP_AXIS_AT_TARGET);
    /* Debounce timing survives HAL millisecond tick rollover. */
    Reset(0xffffffc0U); ShortKey1();
    CHECK(s->axis_state == PUMP_AXIS_MOVING);
    /* Holding a key at boot cannot unexpectedly start either motor. */
    tick = 0; encoder = 0; PumpApp_Init(tick, encoder, -1);
    Poll(1, 1, 1); Poll(1000, 1, 1); Poll(1, 0, 0); Poll(40, 0, 0);
    CHECK(s->axis_state == PUMP_AXIS_IDLE && s->pump_pwm == 0);
    /* Profile limits, monotone travel, continuous velocity and acceleration. */
    for (int direction = -1; direction <= 1; direction += 2) {
        for (int short_move = 0; short_move <= 1; ++short_move) {
            MotionProfile p;
            float position, velocity, last_position = 0, last_velocity = 0;
            float last_accel = 0;
            float distance = short_move ? 0.1f : 7.0f;
            MotionProfile_Init(&p, 0, direction * distance, PUMP_AXIS_MAX_RPM, PUMP_AXIS_MAX_ACCEL, PUMP_AXIS_MAX_JERK);
            CHECK(p.peak_rpm <= PUMP_AXIS_MAX_RPM + .001f && p.duration_s > 0);
            if (short_move) CHECK(p.peak_rpm < PUMP_AXIS_MAX_RPM && p.cruise_s < 0.001f);
            else CHECK(p.peak_rpm > 199.99f && p.cruise_s > .8f && p.duration_s < 3.33f);
            for (int i = 0; i <= (int)(p.duration_s * 100) + 2; ++i) {
                float accel;
                MotionProfile_Sample(&p, i * 0.01f, &position, &velocity);
                CHECK(direction * (position - last_position) >= -0.00001f);
                CHECK(direction * velocity >= -0.00001f && direction * velocity <= PUMP_AXIS_MAX_RPM + .001f);
                accel = (velocity - last_velocity) / 0.01f;
                CHECK(accel > -PUMP_AXIS_MAX_ACCEL - .1f && accel < PUMP_AXIS_MAX_ACCEL + .1f);
                CHECK((accel - last_accel) / 0.01f > -PUMP_AXIS_MAX_JERK - 20 &&
                      (accel - last_accel) / 0.01f < PUMP_AXIS_MAX_JERK + 20);
                last_accel = accel; last_velocity = velocity; last_position = position;
            }
            CHECK(velocity == 0 && position == direction * distance);
        }
    }
    /* Ideal encoder following the S-curve must arrive in both directions. */
    for (int direction = -1; direction <= 1; direction += 2) {
        MotionProfile p;
        float position, velocity;
        Reset(0);
        if (direction == (int)PUMP_AXIS_EXTEND_SIGN) ShortKey1();
        else {
            Poll(1, 1, 0); Poll(40, 1, 0); Poll(820, 1, 0);
            Poll(1, 0, 0); Poll(40, 0, 0);
        }
        MotionProfile_Init(&p, 0, direction * PUMP_AXIS_STEP_REVS, PUMP_AXIS_MAX_RPM, PUMP_AXIS_MAX_ACCEL, PUMP_AXIS_MAX_JERK);
        for (int i = 1; i <= 220; ++i) {
            MotionProfile_Sample(&p, i * .02f, &position, &velocity);
            encoder = (uint16_t)(-(int32_t)(position * PUMP_ENCODER_COUNTS_PER_REV));
            Poll(20, 0, 0);
            CHECK(s->speed_target_rpm <= PUMP_AXIS_MAX_RPM && s->speed_target_rpm >= -PUMP_AXIS_MAX_RPM);
            CHECK(s->axis_state != PUMP_AXIS_TIMEOUT);
        }
        CHECK(s->axis_state == PUMP_AXIS_AT_TARGET && s->speed_target_rpm == 0);
    }
    /* A lagging, quantized motor model exercises position correction and settling. */
    for (int direction = -1; direction <= 1; direction += 2) {
        float actual_rpm = 0, actual_revs = 0, previous_command = 0;
        Reset(0);
        if (direction == (int)PUMP_AXIS_EXTEND_SIGN) ShortKey1();
        else {
            Poll(1, 1, 0); Poll(40, 1, 0); Poll(820, 1, 0);
            Poll(1, 0, 0); Poll(40, 0, 0);
        }
        for (int i = 0; i < 600 && s->axis_state == PUMP_AXIS_MOVING; ++i) {
            previous_command = s->speed_target_rpm;
            actual_rpm += 0.02f / 0.2f * (previous_command - actual_rpm);
            actual_revs += actual_rpm * 0.02f / 60.0f;
            encoder = (uint16_t)(-(int32_t)(actual_revs * PUMP_ENCODER_COUNTS_PER_REV));
            Poll(20, 0, 0);
        }
        CHECK(s->axis_state == PUMP_AXIS_AT_TARGET);
        CHECK(previous_command > -61 && previous_command < 61);
        CHECK(actual_revs * direction > PUMP_AXIS_STEP_REVS - .02f && actual_revs * direction < PUMP_AXIS_STEP_REVS + .02f);
    }
    /* A normal half-second press must extend, not accidentally retract. */
    Reset(0);
    Poll(1, 1, 0); Poll(40, 1, 0); Poll(500, 1, 0);
    Poll(1, 0, 0); Poll(40, 0, 0);
    CHECK(s->target_revs == PUMP_AXIS_EXTEND_SIGN * PUMP_AXIS_STEP_REVS && s->last_key1_action == 1);
    CHECK(s->key1_press_ms >= 500 && s->key1_press_ms < 800);
    /* Minimum starts continuously from zero and reaches creep speed at 300ms. */
    Reset(0); ShortKey1();
    CHECK(s->speed_target_rpm == 0);
    Poll(20, 0, 0);
    CHECK(s->speed_target_rpm > 0 && s->speed_target_rpm < 2);
    Poll(280, 0, 0);
    CHECK(s->speed_target_rpm >= PUMP_AXIS_MIN_RPM);
    /* Wrong-sign encoder feedback must stop early, never run until 15s timeout. */
    Reset(0); ShortKey1();
    encoder = 12; Poll(20, 0, 0);
    CHECK(s->axis_state == PUMP_AXIS_DIRECTION_FAULT && s->speed_target_rpm == 0);
    Poll(500, 0, 0);
    CHECK(s->axis_state == PUMP_AXIS_DIRECTION_FAULT);
    /* Motion can have encoder edges yet fail to track: detect that as a fault. */
    Reset(0); ShortKey1();
    for (int i = 0; i < 100 && s->axis_state == PUMP_AXIS_MOVING; ++i) {
        encoder = (uint16_t)-(int16_t)(i / 10); Poll(20, 0, 0);
    }
    CHECK(s->axis_state == PUMP_AXIS_TRACKING_FAULT && s->speed_target_rpm == 0);
    /* Actual board sign +1: positive drive/increasing raw CNT is valid feedback. */
    tick = 0; encoder = 383; PumpApp_Init(tick, encoder, 1); Poll(41, 0, 0);
    ShortKey1();
    encoder = 395; Poll(20, 0, 0);
    CHECK(s->position_counts == 12 && s->axis_state == PUMP_AXIS_MOVING);
    CHECK(s->speed_target_rpm >= 0); /* Early feedback may lead the S curve: brake, never reverse. */
    encoder = (uint16_t)(383 + (int32_t)(PUMP_AXIS_STEP_REVS * PUMP_ENCODER_COUNTS_PER_REV)); Poll(20, 0, 0);
    CHECK(s->axis_state == PUMP_AXIS_AT_TARGET);
    /* Deceleration must brake rather than reverse, with anti-windup at zero. */
    for (int direction = -1; direction <= 1; direction += 2) {
        PID_t pid;
        float lower = direction > 0 ? 0 : -250;
        float upper = direction > 0 ? 250 : 0;
        float output;
        PID_Init(&pid, 1, .025f, 0, 1000);
        for (int i = 0; i < 100; ++i) {
            output = PID_CalcWithLimits(&pid, direction * 2, direction * 40,
                                        direction * 5, lower, upper);
            CHECK(output == 0 && pid.integral == 0);
        }
        output = PID_CalcWithLimits(&pid, direction * 20, 0,
                                    direction * 100, lower, upper);
        CHECK(output * direction > 100 && output * direction <= 250);
        for (int i = 0; i < 100; ++i) {
            output = PID_CalcWithLimits(&pid, direction * 500, 0,
                                        direction * 500, lower, upper);
            CHECK(output * direction == 250);
        }
    }
    /* Static-friction plant, production planner + PI, feedback sampled at 50ms. */
    for (int direction = -1; direction <= 1; direction += 2) {
        PID_t pid;
        float rpm = 0, revs = 0, output = 0;
        int32_t old_counts = 0;
        Reset(0);
        if (direction == (int)PUMP_AXIS_EXTEND_SIGN) ShortKey1();
        else {
            Poll(1, 1, 0); Poll(40, 1, 0); Poll(820, 1, 0);
            Poll(1, 0, 0); Poll(40, 0, 0);
        }
        PID_Init(&pid, PUMP_SPEED_KP, PUMP_SPEED_KI, 0, 1000);
        for (int i = 1; i < 1200 && s->axis_state == PUMP_AXIS_MOVING; ++i) {
            float magnitude = output * direction;
            float equilibrium = magnitude > 60 ? (magnitude - 60) / .61f : 0;
            if (rpm * direction < 1 && magnitude < 100) equilibrium = 0;
            rpm += .01f / .15f * (direction * equilibrium - rpm);
            revs += rpm * .01f / 60;
            encoder = (uint16_t)(-(int32_t)(revs * PUMP_ENCODER_COUNTS_PER_REV));
            Poll(10, 0, 0);
            if (i % 5 == 0) {
                float count_to_rpm = 1200 / PUMP_ENCODER_COUNTS_PER_REV;
                int32_t counts = s->position_counts;
                output = PID_CalcWithLimits(&pid, s->speed_target_rpm / count_to_rpm,
                    counts - old_counts, 0,
                    direction < 0 ? -250 : 0, direction > 0 ? 250 : 0);
                old_counts = counts;
            }
        }
        CHECK(s->axis_state == PUMP_AXIS_AT_TARGET);
        CHECK(revs * direction > PUMP_AXIS_STEP_REVS - .02f && revs * direction < PUMP_AXIS_STEP_REVS + .02f);
    }
    /* Real command parser + ISR byte assembly + task dispatch, fake UART only. */
    {
        UART_HandleTypeDef uart = {0};
        uart.gState = HAL_UART_STATE_READY;
        Reset(0); BT_Control_Init(&uart);
        Receive(&uart, "EXTEND\n");
        CHECK(s->axis_state == PUMP_AXIS_IDLE && !BT_Control_IsAppMode());
        Receive(&uart, "AP"); CHECK(!BT_Control_IsAppMode());
        Receive(&uart, "P\r\n"); CHECK(BT_Control_IsAppMode());
        CHECK(strstr(uart_output, "OK APP V1") != NULL);
        Receive(&uart, "step 2.5\nSPEED 120\nEXTEND\n");
        CHECK(s->axis_state == PUMP_AXIS_MOVING && s->target_revs == PUMP_AXIS_EXTEND_SIGN * 2.5f);
        CHECK(PumpApp_GetSpeed() == 120 && PumpApp_GetStep() == 2.5f);
        Receive(&uart, "EXTEND\nSTEP 3\nSPEED 150\n");
        CHECK(s->target_revs == PUMP_AXIS_EXTEND_SIGN * 2.5f && PumpApp_GetStep() == 2.5f && PumpApp_GetSpeed() == 120);
        CHECK(strstr(uart_output, "ERR BUSY") != NULL);
        Receive(&uart, "PUMP ON\nSTOP\n");
        CHECK(s->axis_state == PUMP_AXIS_IDLE && s->pump_pwm == 0);
        Receive(&uart, "SPEED 201\nSPEED 59\nSPEED 60.5\nSTEP NAN\nSTEP -1\nSTEP 11\nSTEP 1.2x\nPUMP 1001\n");
        CHECK(PumpApp_GetStep() == 2.5f && PumpApp_GetSpeed() == 120 && s->pump_pwm == 0);
        CHECK(strstr(uart_output, "ERR RANGE") != NULL);
        Receive(&uart, "PUMP 321\nSTATUS\n"); CHECK(s->pump_pwm == 321);
        BT_Control_Task(); CHECK(strstr(uart_output, "STATE AXIS=") != NULL);
        tick += 4000; Receive(&uart, "PING\n");
        tick += 4000; BT_Control_Task(); CHECK(s->pump_pwm == 321);
        tick += 1001; BT_Control_Task(); CHECK(s->pump_pwm == 0);
        CHECK(strstr(uart_output, "ERR HEARTBEAT STOPPED") != NULL);
        Receive(&uart, "PUMP ON\n");
        Receive(&uart, "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXSTOP\n");
        CHECK(s->pump_pwm == 800); /* Overlong line suffix cannot execute as a command. */
        Receive(&uart, "STOP\n"); CHECK(s->pump_pwm == 0);
        /* RX overflow discards the damaged line and recovers at a newline. */
        for (unsigned i = 0; i < 300; ++i) { *uart_rx = 'X'; HAL_UART_RxCpltCallback(&uart); }
        BT_Control_Task(); Receive(&uart, "\nPUMP ON\nSTOP\n"); CHECK(s->pump_pwm == 0);
        /* A physical key held during remote STOP must not restart on release. */
        Poll(1, 1, 0); Poll(40, 1, 0);
        Receive(&uart, "STOP\n"); Poll(100, 0, 0); Poll(40, 0, 0);
        CHECK(s->axis_state == PUMP_AXIS_IDLE);
        Receive(&uart, "RETRACT\n"); CHECK(s->target_revs == -PUMP_AXIS_EXTEND_SIGN * 2.5f);
        Receive(&uart, "STOP\nVOFA\n"); CHECK(!BT_Control_IsAppMode());
    }
    return 0;
}
