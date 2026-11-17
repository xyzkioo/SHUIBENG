/*
 * 立创开发板软硬件资料与相关扩展板软硬件资料官网全部开源
 * 开发板官网：www.lckfb.com
 * 技术支持常驻论坛，任何技术问题欢迎随时交流学习
 * 立创论坛：https://oshwhub.com/forum
 * 关注bilibili账号：【立创开发板】，掌握我们的最新动态！
 * 不靠卖板赚钱，以培养中国工程师为己任
 * Change Logs:
 * Date           Author       Notes
 * 2024-04-02     LCKFB-LP     first version (标准库 SPL，示例引脚 PA1/PA2/PA6)
 * 2026-11-12     ---          移植到 HAL 库，引脚改为本工程实际接法（PB 口）
 */

#ifndef __TB6612_H__
#define __TB6612_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/*====================================================================
 * 一、引脚映射区（本工程实际接法）
 *--------------------------------------------------------------------
 *   A 通道： AIN1 -> PB13     AIN2 -> PB12     PWMA -> PB1  (TIM3_CH4)
 *   B 通道： BIN1 -> PB14     BIN2 -> PB15     PWMB -> PB0  (TIM3_CH3)
 *   STBY  -> 接 3.3V（若由 MCU 控制，告诉我改用哪个脚）
 *--------------------------------------------------------------------
 * 两路 PWM 都在 TIM3 上，共用一个时基，所以调速范围也共用（per）。
 * 以后换脚只改这一段即可，其它代码不用动。
 *====================================================================*/

/* ---- A 通道方向脚 ---- */
#define TB_AIN1_PORT            GPIOB
#define TB_AIN1_PIN             GPIO_PIN_13
#define TB_AIN1_CLK_ENABLE()    __HAL_RCC_GPIOB_CLK_ENABLE()

#define TB_AIN2_PORT            GPIOB
#define TB_AIN2_PIN             GPIO_PIN_12
#define TB_AIN2_CLK_ENABLE()    __HAL_RCC_GPIOB_CLK_ENABLE()

/* ---- B 通道方向脚 ---- */
#define TB_BIN1_PORT            GPIOB
#define TB_BIN1_PIN             GPIO_PIN_14
#define TB_BIN1_CLK_ENABLE()    __HAL_RCC_GPIOB_CLK_ENABLE()

#define TB_BIN2_PORT            GPIOB
#define TB_BIN2_PIN             GPIO_PIN_15
#define TB_BIN2_CLK_ENABLE()    __HAL_RCC_GPIOB_CLK_ENABLE()

/* ---- PWM 引脚 + 定时器通道（两路共用 TIM3） ---- */
#define TB_PWMA_PORT            GPIOB
#define TB_PWMA_PIN             GPIO_PIN_1           /* PB1 = TIM3_CH4 */
#define TB_PWMA_CHANNEL         TIM_CHANNEL_4

#define TB_PWMB_PORT            GPIOB
#define TB_PWMB_PIN             GPIO_PIN_0           /* PB0 = TIM3_CH3 */
#define TB_PWMB_CHANNEL         TIM_CHANNEL_3

#define TB_PWM_GPIO_CLK_ENABLE()   __HAL_RCC_GPIOB_CLK_ENABLE()
#define TB_PWM_TIM                 TIM3
#define TB_PWM_TIM_CLK_ENABLE()    __HAL_RCC_TIM3_CLK_ENABLE()

/*====================================================================
 * 二、方向电平宏（对应原 SPL 版的 GPIO_WriteBit）
 *====================================================================*/
#define AIN1_OUT(x)   HAL_GPIO_WritePin(TB_AIN1_PORT, TB_AIN1_PIN, (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define AIN2_OUT(x)   HAL_GPIO_WritePin(TB_AIN2_PORT, TB_AIN2_PIN, (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define BIN1_OUT(x)   HAL_GPIO_WritePin(TB_BIN1_PORT, TB_BIN1_PIN, (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define BIN2_OUT(x)   HAL_GPIO_WritePin(TB_BIN2_PORT, TB_BIN2_PIN, (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)

/*====================================================================
 * 三、对外接口
 *====================================================================*/

/**
 * @brief  初始化 TB6612（GPIO + TIM3 双路 PWM 全部由本函数完成）
 * @param  pre 预分频值，实际分频 = pre，范围 1~65536
 * @param  per 自动重装载值(ARR)，PWM 频率 = fTIM3 / (pre * (per + 1))
 * @retval HAL_OK / HAL_ERROR
 * @note   本工程主频为 HSI 8MHz：TB6612_Init(8, 1000) -> 约 1kHz
 *         若以后改成 72MHz PLL   ：TB6612_Init(72, 1000) -> 约 1kHz
 */
HAL_StatusTypeDef TB6612_Init(uint16_t pre, uint16_t per);

/**
 * @brief  复用 CubeMX 已生成的 TIM3 句柄（需已配置 CH3、CH4 为 PWM）
 * @param  htim 例如 &htim3
 * @retval HAL_OK / HAL_ERROR
 * @note   调用前请在 CubeMX 里把 TIM3 的 Prescaler/Period 设成本工程要的值，
 *         否则沿用 CubeMX 里的 0 / 65535（约 122Hz，电机抖动明显）
 */
HAL_StatusTypeDef TB6612_InitWithHandle(TIM_HandleTypeDef *htim);

/**
 * @brief  A 通道电机控制
 * @param  dir   旋转方向：1 正转，0 反转
 * @param  speed 占空比，范围 0 ~ per（对应 CCR 值）
 */
void AO_Control(uint8_t dir, uint32_t speed);

/**
 * @brief  B 通道电机控制
 * @param  dir   旋转方向：1 正转，0 反转
 * @param  speed 占空比，范围 0 ~ per（对应 CCR 值）
 */
void BO_Control(uint8_t dir, uint32_t speed);

/** @brief A 通道停止（AIN1=AIN2=0，自由滑行） */
void AO_Stop(void);

/** @brief A 通道刹车（AIN1=AIN2=1，绕组短路制动） */
void AO_Brake(void);

/** @brief B 通道停止（BIN1=BIN2=0，自由滑行） */
void BO_Stop(void);

/** @brief B 通道刹车（BIN1=BIN2=1，绕组短路制动） */
void BO_Brake(void);

/** @brief 两路同时停止 */
void TB6612_Stop(void);

/** @brief 获取当前 PWM 周期值(ARR)，用于把百分比换算成 speed */
uint32_t TB6612_GetPeriod(void);

#ifdef __cplusplus
}
#endif

#endif /* __TB6612_H__ */
