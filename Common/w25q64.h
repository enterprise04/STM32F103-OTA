#ifndef __W25Q64_H
#define __W25Q64_H

#include "stm32f1xx_hal.h"

/* 片选引脚(CubeMX 中 PA4 已配为 GPIO_Output,初始 High) */
#define W25_CS_PORT         GPIOA
#define W25_CS_PIN          GPIO_PIN_4

#define W25_JEDEC_ID        0xEF4017UL  /* W25Q64: Winbond / SPI / 64Mbit */
#define W25_PAGE_SIZE       256U
#define W25_SECTOR_SIZE     4096U

/* W25Q64 分区(见 docs/01_总体设计.md 第 3 节) */
#define W25_PART_PARAM      0x000000UL  /* 参数区:升级标志/版本/CRC      */
#define W25_PART_DOWNLOAD   0x010000UL  /* 下载区:新固件暂存             */
#define W25_PART_BACKUP     0x020000UL  /* 备份区:旧固件,用于回滚       */
#define W25_PART_SIZE       0x010000UL  /* 各 64KB                        */
#define W25_TEST_ADDR       0x030000UL  /* 空闲区,读写自检用             */

/* 返回 0 成功,-1 失败(下同) */
int      W25_Init(void);                /* 读 JEDEC ID 并核对 */
uint32_t W25_ReadJEDEC(void);
int      W25_Read(uint32_t addr, uint8_t *buf, uint32_t len);
int      W25_EraseSector(uint32_t addr);            /* 4KB 扇区擦除,addr 需 4K 对齐 */
int      W25_Write(uint32_t addr, const uint8_t *buf, uint32_t len);  /* 自动跨页,目标区域需已擦除 */

#endif /* __W25Q64_H */
