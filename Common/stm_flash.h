#ifndef __STM_FLASH_H
#define __STM_FLASH_H

#include "stm32f1xx_hal.h"

/* 内部 Flash 擦写(C8T6 中容量产品,页大小 1KB)。返回 0 成功,-1 失败 */
int STM_EraseRegion(uint32_t addr, uint32_t size);      /* addr 需 1K 对齐 */
int STM_Write(uint32_t addr, const uint8_t *buf, uint32_t len);  /* 目标需已擦除;奇数长度自动补 0xFF */

#endif /* __STM_FLASH_H */
