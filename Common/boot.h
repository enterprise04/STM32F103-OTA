#ifndef __BOOT_H
#define __BOOT_H

#include "stm32f1xx_hal.h"

/* Flash 分区(见 docs/01_总体设计.md) */
#define BOOT_ADDR       0x08000000UL    /* Bootloader 起始地址          */
#define BOOT_SIZE       0x4000UL        /* 16KB                         */
#define APP_ADDR        0x08004000UL    /* App 起始地址                 */
#define APP_MAX_SIZE    0xC000UL        /* 48KB                         */

/* C8T6 SRAM 范围,用于校验 App 栈顶合法性 */
#define SRAM_END        (SRAM_BASE + 20U * 1024U)

/* App 区是否存在合法固件(栈顶指针落在 SRAM 内) */
uint8_t Boot_AppValid(uint32_t app_addr);

/* 清理现场并跳转到 App,成功则不返回 */
void Boot_JumpToApp(uint32_t app_addr);

#endif /* __BOOT_H */
