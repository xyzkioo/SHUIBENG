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
 * 2026-11-12     ---          移植到 HAL 库，引脚改为本工程实际接法（PB 口），
 *                             并补齐 B 通道接口
 */

#include "tb6612.h"

/*====================================================================
 * 私有变量
 *====================================================================*/
static TIM_HandleTypeDef  s_tb_tim;         /* 独立句柄，只在 TB6612_Init 里用到 */
static TIM_HandleTypeDef *s_htim   = NULL;  /* 实际使用的定时器句柄 */
static uint32_t           s_period = 0U;    /* ARR，用于 speed 限幅 */

/*====================================================================
 * 私有函数：GPIO 初始化
 *   PB13/PB12 AIN1/AIN2、PB14/PB15 BIN1/BIN2   推挽输出
 *   PB1/PB0   PWMA/PWMB                        复用推挽（定时器通道）
 *====================================================================*/
static void TB6612_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    TB_AIN1_CLK_ENABLE();
    TB_AIN2_CLK_ENABLE();
    TB_BIN1_CLK_ENABLE();
    TB_BIN2_CLK_ENABLE();
    TB_PWM_GPIO_CLK_ENABLE();

    /* 四个方向脚：推挽输出 */
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    GPIO_InitStruct.Pin = TB_AIN1_PIN;
    HAL_GPIO_Init(TB_AIN1_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = TB_AIN2_PIN;
    HAL_GPIO_Init(TB_AIN2_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = TB_BIN1_PIN;
    HAL_GPIO_Init(TB_BIN1_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = TB_BIN2_PIN;
    HAL_GPIO_Init(TB_BIN2_PORT, &GPIO_InitStruct);

    /* 两路 PWM 脚：复用推挽，交给 TIM3 通道驱动 */
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    GPIO_InitStruct.Pin = TB_PWMA_PIN;
    HAL_GPIO_Init(TB_PWMA_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = TB_PWMB_PIN;
    HAL_GPIO_Init(TB_PWMB_PORT, &GPIO_InitStruct);

    /* 上电默认两路都停转 */
    AIN1_OUT(0);
    AIN2_OUT(0);
    BIN1_OUT(0);
    BIN2_OUT(0);
}

/*====================================================================
 * 函数名称：TB6612_Init
 * 功    能：TB6612 初始化（GPIO + TIM3 CH3/CH4 双路 PWM），对应原 SPL 版
 * 参    数：pre 预分频，per PWM 周期
 * 返 回 值：HAL_OK / HAL_ERROR
 * 备    注：PWM 频率 = fTIM3 / (pre * (per + 1))
 *====================================================================*/
HAL_StatusTypeDef TB6612_Init(uint16_t pre, uint16_t per)
{
    TIM_OC_InitTypeDef sConfigOC = {0};

    if (pre == 0U) { pre = 1U; }
    if (per == 0U) { per = 1U; }

    /* 1. GPIO：方向脚 + PWM 脚 */
    TB6612_GPIO_Init();

    /* 2. 定时器时基（PSC / ARR 寄存器，写法和 SPL 完全一致） */
    TB_PWM_TIM_CLK_ENABLE();

    s_tb_tim.Instance               = TB_PWM_TIM;
    s_tb_tim.Init.Prescaler         = (uint32_t)(pre - 1U);  /* 分频值 = pre */
    s_tb_tim.Init.CounterMode       = TIM_COUNTERMODE_UP;
    s_tb_tim.Init.Period            = (uint32_t)per;         /* ARR = per，与 SPL 一致 */
    s_tb_tim.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    s_tb_tim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&s_tb_tim) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* 3. 两路 PWM 通道：PWM1 模式、高电平有效
     *    HAL_TIM_PWM_ConfigChannel 内部会打开 CCRx 预装载(OCxPE)，
     *    等价于原代码的 TIM_OC1PreloadConfig(ENABLE) */
    sConfigOC.OCMode     = TIM_OCMODE_PWM1;
    sConfigOC.Pulse      = 0U;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

    if (HAL_TIM_PWM_ConfigChannel(&s_tb_tim, &sConfigOC, TB_PWMA_CHANNEL) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if (HAL_TIM_PWM_ConfigChannel(&s_tb_tim, &sConfigOC, TB_PWMB_CHANNEL) != HAL_OK)
    {
        return HAL_ERROR;
    }

    s_htim   = &s_tb_tim;
    s_period = (uint32_t)per;

    /* 4. 启动两路 PWM 输出，等价于原代码的 TIM_Cmd(TIM3, ENABLE) */
    if (HAL_TIM_PWM_Start(s_htim, TB_PWMA_CHANNEL) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if (HAL_TIM_PWM_Start(s_htim, TB_PWMB_CHANNEL) != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/*====================================================================
 * 函数名称：TB6612_InitWithHandle
 * 功    能：复用 CubeMX 已生成的 TIM3 句柄，只做 GPIO + 启动 PWM
 * 参    数：htim 已初始化的 PWM 定时器句柄（例如 &htim3，需已配 CH3/CH4）
 * 返 回 值：HAL_OK / HAL_ERROR
 *====================================================================*/
HAL_StatusTypeDef TB6612_InitWithHandle(TIM_HandleTypeDef *htim)
{
    if (htim == NULL)
    {
        return HAL_ERROR;
    }

    TB6612_GPIO_Init();

    s_htim   = htim;
    s_period = __HAL_TIM_GET_AUTORELOAD(htim);

    if (HAL_TIM_PWM_Start(htim, TB_PWMA_CHANNEL) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if (HAL_TIM_PWM_Start(htim, TB_PWMB_CHANNEL) != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/*====================================================================
 * 函数名称：AO_Control
 * 功    能：A 端口电机控制
 * 参    数：dir 1 正转 0 反转；speed 0 ~ per
 *====================================================================*/
void AO_Control(uint8_t dir, uint32_t speed)
{
    if (s_htim == NULL)
    {
        return;
    }

    if (dir == 1U)
    {
        AIN1_OUT(0);
        AIN2_OUT(1);
    }
    else
    {
        AIN1_OUT(1);
        AIN2_OUT(0);
    }

    if (speed > s_period)
    {
        speed = s_period;
    }

    /* 等价于原代码的 TIM_SetCompare1(TIM3, speed) */
    __HAL_TIM_SET_COMPARE(s_htim, TB_PWMA_CHANNEL, speed);
}

/*====================================================================
 * 函数名称：BO_Control
 * 功    能：B 端口电机控制
 * 参    数：dir 1 正转 0 反转；speed 0 ~ per
 *====================================================================*/
void BO_Control(uint8_t dir, uint32_t speed)
{
    if (s_htim == NULL)
    {
        return;
    }

    if (dir == 1U)
    {
        BIN1_OUT(0);
        BIN2_OUT(1);
    }
    else
    {
        BIN1_OUT(1);
        BIN2_OUT(0);
    }

    if (speed > s_period)
    {
        speed = s_period;
    }

    __HAL_TIM_SET_COMPARE(s_htim, TB_PWMB_CHANNEL, speed);
}

/*====================================================================
 * 函数名称：AO_Stop / AO_Brake / BO_Stop / BO_Brake / TB6612_Stop
 * 功    能：停止（自由滑行）/ 刹车（短路制动）
 *====================================================================*/
void AO_Stop(void)
{
    if (s_htim == NULL) { return; }

    __HAL_TIM_SET_COMPARE(s_htim, TB_PWMA_CHANNEL, 0U);
    AIN1_OUT(0);
    AIN2_OUT(0);
}

void AO_Brake(void)
{
    if (s_htim == NULL) { return; }

    __HAL_TIM_SET_COMPARE(s_htim, TB_PWMA_CHANNEL, 0U);
    AIN1_OUT(1);
    AIN2_OUT(1);
}

void BO_Stop(void)
{
    if (s_htim == NULL) { return; }

    __HAL_TIM_SET_COMPARE(s_htim, TB_PWMB_CHANNEL, 0U);
    BIN1_OUT(0);
    BIN2_OUT(0);
}

void BO_Brake(void)
{
    if (s_htim == NULL) { return; }

    __HAL_TIM_SET_COMPARE(s_htim, TB_PWMB_CHANNEL, 0U);
    BIN1_OUT(1);
    BIN2_OUT(1);
}

void TB6612_Stop(void)
{
    AO_Stop();
    BO_Stop();
}

/*====================================================================
 * 函数名称：TB6612_GetPeriod
 * 功    能：获取 PWM 周期值(ARR)
 *====================================================================*/
uint32_t TB6612_GetPeriod(void)
{
    return s_period;
}
