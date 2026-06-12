#include "boot.h"

typedef void (*pFunction)(void);

uint8_t Boot_AppValid(uint32_t app_addr)
{
    uint32_t sp = *(volatile uint32_t *)app_addr;
    return (sp > SRAM_BASE) && (sp <= SRAM_END);
}

void Boot_JumpToApp(uint32_t app_addr)
{
    uint32_t  sp         = *(volatile uint32_t *)app_addr;
    uint32_t  reset_addr = *(volatile uint32_t *)(app_addr + 4U);
    pFunction app_reset  = (pFunction)reset_addr;
    uint32_t  i;

    if (!Boot_AppValid(app_addr)) {
        return;
    }

    /* 关外设、恢复时钟到复位态,避免 App 在脏环境下启动 */
    HAL_RCC_DeInit();
    HAL_DeInit();

    /* 关 SysTick,清除所有 NVIC 使能与挂起标志 */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;
    __disable_irq();            /* App 入口处需 __enable_irq() */
    for (i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    __set_MSP(sp);
    __set_CONTROL(0);           /* 特权模式 + MSP */
    app_reset();                /* 不返回 */
}
