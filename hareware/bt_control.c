#include "bt_control.h"
#include "pump_app.h"
#include <stdio.h>
#include <string.h>

#define RX_SIZE 256U
#define TX_SIZE 512U
#define LINE_SIZE 48U
#define HEARTBEAT_MS 5000U

static UART_HandleTypeDef *port;
static uint8_t rx_byte, rx[RX_SIZE], tx[TX_SIZE], tx_chunk[128];
static volatile uint16_t rx_head, rx_tail;
static volatile uint8_t rx_overflow;
static uint16_t tx_head, tx_tail, line_length;
static char line[LINE_SIZE];
static uint8_t discard_line, app_mode, remote_active;
static uint32_t last_command, last_report;
static int actual_rpm, actual_pwm;

static int Enqueue(const char *text)
{
    uint16_t length = (uint16_t)strlen(text);
    uint16_t free_space = (uint16_t)((tx_tail + TX_SIZE - tx_head - 1U) % TX_SIZE);
    if (length > free_space) return 0; /* Never send half a line. */
    while (length--) {
        tx[tx_head] = (uint8_t)*text++;
        tx_head = (uint16_t)((tx_head + 1U) % TX_SIZE);
    }
    return 1;
}

static void Status(char *reply, uint16_t size, const char *prefix)
{
    const PumpAppStatus *s = PumpApp_GetStatus();
    (void)snprintf(reply, size,
        "%s AXIS=%u POS=%ld TARGET=%ld RPM=%d PWM=%d PUMP=%u STEP=%ld SPEED=%u\r\n",
        prefix, (unsigned)s->axis_state,
        (long)(s->position_counts * (1000.0f / PUMP_ENCODER_COUNTS_PER_REV)),
        (long)(s->target_revs * 1000.0f), actual_rpm, actual_pwm, (unsigned)s->pump_pwm,
        (long)(PumpApp_GetStep() * 1000.0f + 0.5f), (unsigned)PumpApp_GetSpeed());
}

/* Strict positive decimal -> thousandths; rejects signs, NaN, junk and overflow. */
static int Number(const char *text, uint32_t *value)
{
    uint32_t whole = 0, fraction = 0, scale = 100;
    unsigned digits = 0;
    while (*text >= '0' && *text <= '9') {
        whole = whole * 10U + (unsigned)(*text++ - '0');
        if (whole > 1000U) return 0;
        ++digits;
    }
    if (!digits) return 0;
    if (*text == '.') {
        ++text; digits = 0;
        while (*text >= '0' && *text <= '9' && scale) {
            fraction += (unsigned)(*text++ - '0') * scale;
            scale /= 10U; ++digits;
        }
        if (!digits) return 0;
    }
    if (*text) return 0;
    *value = whole * 1000U + fraction;
    return 1;
}

void BT_Control_Command(const char *command, uint32_t now, char *reply, uint16_t size)
{
    char text[LINE_SIZE];
    uint32_t value;
    unsigned length = 0;
    const char *error = NULL;
    if (!size) return;
    reply[0] = 0;
    while (*command == ' ') ++command;
    while (*command && length < sizeof(text) - 1U) {
        char c = *command++;
        text[length++] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
    }
    if (*command) { (void)snprintf(reply, size, "ERR LENGTH\r\n"); return; }
    while (length && text[length - 1U] == ' ') --length;
    text[length] = 0;
    if (!strcmp(text, "APP")) {
        app_mode = 1U;
        last_command = now;
        (void)snprintf(reply, size, "\r\nOK APP V1\r\n");
        return;
    }
    /* STOP always works, even before the app handshake. */
    if (!strcmp(text, "STOP")) {
        PumpApp_StopAll(); remote_active = 0U;
        if (app_mode) (void)snprintf(reply, size, "OK STOP\r\n");
        return;
    }
    if (!app_mode) return;
    if (!strcmp(text, "PING")) {
        (void)snprintf(reply, size, "OK PONG\r\n");
    } else if (!strcmp(text, "STATUS")) {
        Status(reply, size, "STATE");
    } else if (!strcmp(text, "VOFA")) {
        if (PumpApp_GetStatus()->axis_state == PUMP_AXIS_MOVING || PumpApp_GetStatus()->pump_pwm)
            error = "BUSY";
        else { app_mode = remote_active = 0U; return; }
    } else if (!strcmp(text, "EXTEND") || !strcmp(text, "RETRACT")) {
        if (!PumpApp_StartMove(now, text[0] == 'R')) error = "BUSY";
        else { remote_active = 1U; (void)snprintf(reply, size, "OK %s\r\n", text); }
    } else if (!strcmp(text, "AXIS STOP")) {
        PumpApp_StopAxis(); (void)snprintf(reply, size, "OK AXIS STOP\r\n");
    } else if (!strcmp(text, "PUMP ON") || !strcmp(text, "PUMP OFF")) {
        int on = !strcmp(text, "PUMP ON");
        (void)PumpApp_SetPump(on ? PUMP_RUN_PWM : 0U);
        if (on) remote_active = 1U;
        (void)snprintf(reply, size, "OK %s\r\n", text);
    } else if (!strncmp(text, "PUMP ", 5)) {
        if (!Number(text + 5, &value) || value > 1000000U || value % 1000U) error = "RANGE";
        else {
            (void)PumpApp_SetPump((uint16_t)(value / 1000U));
            if (value) remote_active = 1U;
            (void)snprintf(reply, size, "OK PUMP\r\n");
        }
    } else if (!strncmp(text, "SPEED ", 6) || !strncmp(text, "STEP ", 5)) {
        int speed = text[1] == 'P';
        if (PumpApp_GetStatus()->axis_state == PUMP_AXIS_MOVING) error = "BUSY";
        else if (!Number(text + (speed ? 6 : 5), &value)) error = "RANGE";
        else if (speed ? (value % 1000U || !PumpApp_SetSpeed(value / 1000.0f)) :
                         !PumpApp_SetStep(value / 1000.0f)) error = "RANGE";
        else (void)snprintf(reply, size, "OK %s\r\n", speed ? "SPEED" : "STEP");
    } else error = "COMMAND";
    if (error) (void)snprintf(reply, size, "ERR %s\r\n", error);
    else last_command = now;
}

void BT_Control_Init(UART_HandleTypeDef *uart)
{
    port = uart;
    rx_head = rx_tail = tx_head = tx_tail = line_length = 0;
    rx_overflow = discard_line = app_mode = remote_active = 0;
    last_command = last_report = HAL_GetTick();
    (void)HAL_UART_Receive_IT(port, &rx_byte, 1U);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart != port) return;
    uint16_t next = (uint16_t)((rx_head + 1U) % RX_SIZE);
    if (next == rx_tail) rx_overflow = 1U;
    else { rx[rx_head] = rx_byte; rx_head = next; }
    (void)HAL_UART_Receive_IT(port, &rx_byte, 1U);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart != port) return;
    rx_overflow = 1U;
    __HAL_UART_CLEAR_OREFLAG(port);
    if (port->RxState == HAL_UART_STATE_READY) (void)HAL_UART_Receive_IT(port, &rx_byte, 1U);
}

void BT_Control_Task(void)
{
    char reply[192];
    uint32_t now = HAL_GetTick();
    if (rx_overflow) {
        uint32_t primask = __get_PRIMASK();
        __disable_irq(); rx_tail = rx_head; rx_overflow = 0; __set_PRIMASK(primask);
        discard_line = 1U; line_length = 0;
        if (app_mode) (void)Enqueue("ERR RX\r\n");
    }
    /* Each pass is bounded so incoming traffic cannot starve the speed loop. */
    unsigned budget = RX_SIZE;
    while (rx_tail != rx_head && budget--) {
        char c = (char)rx[rx_tail]; rx_tail = (uint16_t)((rx_tail + 1U) % RX_SIZE);
        if (c == '\r') continue;
        if (c == '\n') {
            if (!discard_line && line_length) {
                line[line_length] = 0;
                uint16_t free_space = (uint16_t)((tx_tail + TX_SIZE - tx_head - 1U) % TX_SIZE);
                if (free_space >= sizeof(reply)) {
                    BT_Control_Command(line, now, reply, sizeof(reply));
                    if (reply[0]) (void)Enqueue(reply);
                } else {
                    /* Never start an operation when its acknowledgement cannot be queued. */
                    PumpApp_StopAll(); remote_active = 0;
                    if (app_mode) (void)Enqueue("ERR TX BUSY STOPPED\r\n");
                }
            } else if (discard_line && app_mode) (void)Enqueue("ERR LINE\r\n");
            line_length = 0; discard_line = 0;
        } else if ((unsigned char)c < 32U || (unsigned char)c > 126U || line_length >= LINE_SIZE - 1U) {
            discard_line = 1U;
        } else if (!discard_line) line[line_length++] = c;
    }
    if (remote_active) {
        if (PumpApp_GetStatus()->axis_state != PUMP_AXIS_MOVING && !PumpApp_GetStatus()->pump_pwm)
            remote_active = 0;
        else if (now - last_command > HEARTBEAT_MS) {
            PumpApp_StopAll(); remote_active = 0;
            (void)Enqueue("ERR HEARTBEAT STOPPED\r\n");
        }
    }
    if (port && port->gState == HAL_UART_STATE_READY && tx_tail != tx_head) {
        uint16_t count = 0, tail = tx_tail;
        while (tail != tx_head && count < sizeof(tx_chunk)) {
            tx_chunk[count++] = tx[tail]; tail = (uint16_t)((tail + 1U) % TX_SIZE);
        }
        if (HAL_UART_Transmit_IT(port, tx_chunk, count) == HAL_OK) tx_tail = tail;
    }
}

void BT_Control_SendTelemetry(float target, float actual, float pwm)
{
    static uint8_t frame[16];
    const float channels[3] = {target, actual, pwm};
    actual_rpm = (int)actual; actual_pwm = (int)pwm;
    if (app_mode) {
        uint32_t now = HAL_GetTick();
        if (now - last_report >= 200U && tx_head == tx_tail) {
            char message[192]; Status(message, sizeof(message), "DATA");
            (void)Enqueue(message); last_report = now;
        }
    } else if (port && port->gState == HAL_UART_STATE_READY && tx_head == tx_tail) {
        _Static_assert(sizeof(float) == 4U, "JustFloat requires 32-bit float");
        memcpy(frame, channels, sizeof(channels));
        frame[12] = 0; frame[13] = 0; frame[14] = 0x80; frame[15] = 0x7f;
        (void)HAL_UART_Transmit_IT(port, frame, sizeof(frame));
    }
}

int BT_Control_IsAppMode(void) { return app_mode; }
