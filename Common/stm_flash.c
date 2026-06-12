#include "stm_flash.h"

int STM_EraseRegion(uint32_t addr, uint32_t size)
{
    FLASH_EraseInitTypeDef ei;
    uint32_t page_err = 0;
    HAL_StatusTypeDef st;

    ei.TypeErase   = FLASH_TYPEERASE_PAGES;
    ei.PageAddress = addr;
    ei.NbPages     = (size + FLASH_PAGE_SIZE - 1U) / FLASH_PAGE_SIZE;

    HAL_FLASH_Unlock();
    st = HAL_FLASHEx_Erase(&ei, &page_err);
    HAL_FLASH_Lock();
    return (st == HAL_OK) ? 0 : -1;
}

int STM_Write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t i;
    uint16_t hw;
    HAL_StatusTypeDef st = HAL_OK;

    HAL_FLASH_Unlock();
    for (i = 0; (i + 1U) < len && st == HAL_OK; i += 2U) {
        hw = (uint16_t)(buf[i] | ((uint16_t)buf[i + 1U] << 8));
        st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, hw);
    }
    if (st == HAL_OK && (len & 1U)) {           /* 末尾奇字节,高 8 位补 0xFF */
        hw = (uint16_t)(buf[len - 1U] | 0xFF00U);
        st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + len - 1U, hw);
    }
    HAL_FLASH_Lock();
    return (st == HAL_OK) ? 0 : -1;
}
