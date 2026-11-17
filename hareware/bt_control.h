#ifndef BT_CONTROL_H
#define BT_CONTROL_H
#include "stm32f1xx_hal.h"

void BT_Control_Init(UART_HandleTypeDef *uart);
void BT_Control_Task(void);
void BT_Control_SendTelemetry(float target_rpm, float actual_rpm, float pwm);
/* Main-context parser, also usable by tests. One command without CR/LF. */
void BT_Control_Command(const char *command, uint32_t now, char *reply, uint16_t size);
int BT_Control_IsAppMode(void);

#endif
