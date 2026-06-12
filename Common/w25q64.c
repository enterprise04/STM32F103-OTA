#include "w25q64.h"

extern SPI_HandleTypeDef hspi1;

/* W25Q64 指令集(仅用到的部分) */
#define CMD_WRITE_ENABLE    0x06
#define CMD_READ_SR1        0x05
#define CMD_READ_DATA       0x03
#define CMD_PAGE_PROGRAM    0x02
#define CMD_SECTOR_ERASE_4K 0x20
#define CMD_JEDEC_ID        0x9F

#define SR1_BUSY            0x01

#define CS_LOW()    HAL_GPIO_WritePin(W25_CS_PORT, W25_CS_PIN, GPIO_PIN_RESET)
#define CS_HIGH()   HAL_GPIO_WritePin(W25_CS_PORT, W25_CS_PIN, GPIO_PIN_SET)

static uint8_t spi_xfer(uint8_t tx)
{
    uint8_t rx = 0xFF;
    HAL_SPI_TransmitReceive(&hspi1, &tx, &rx, 1, 100);
    return rx;
}

static void send_cmd_addr(uint8_t cmd, uint32_t addr)
{
    uint8_t buf[4];
    buf[0] = cmd;
    buf[1] = (uint8_t)(addr >> 16);
    buf[2] = (uint8_t)(addr >> 8);
    buf[3] = (uint8_t)addr;
    HAL_SPI_Transmit(&hspi1, buf, 4, 100);
}

static void write_enable(void)
{
    CS_LOW();
    spi_xfer(CMD_WRITE_ENABLE);
    CS_HIGH();
}

/* 轮询 SR1.BUSY,擦除/编程完成后清零 */
static int wait_busy(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    uint8_t  sr;

    do {
        CS_LOW();
        spi_xfer(CMD_READ_SR1);
        sr = spi_xfer(0xFF);
        CS_HIGH();
        if ((sr & SR1_BUSY) == 0) {
            return 0;
        }
    } while (HAL_GetTick() - t0 < timeout_ms);
    return -1;
}

uint32_t W25_ReadJEDEC(void)
{
    uint32_t id;

    CS_LOW();
    spi_xfer(CMD_JEDEC_ID);
    id  = (uint32_t)spi_xfer(0xFF) << 16;
    id |= (uint32_t)spi_xfer(0xFF) << 8;
    id |= (uint32_t)spi_xfer(0xFF);
    CS_HIGH();
    return id;
}

int W25_Init(void)
{
    return (W25_ReadJEDEC() == W25_JEDEC_ID) ? 0 : -1;
}

int W25_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    HAL_StatusTypeDef st;

    CS_LOW();
    send_cmd_addr(CMD_READ_DATA, addr);
    st = HAL_SPI_Receive(&hspi1, buf, (uint16_t)len, 1000);
    CS_HIGH();
    return (st == HAL_OK) ? 0 : -1;
}

int W25_EraseSector(uint32_t addr)
{
    write_enable();
    CS_LOW();
    send_cmd_addr(CMD_SECTOR_ERASE_4K, addr);
    CS_HIGH();
    return wait_busy(1000);    /* 4K 擦除典型 45ms,最大 400ms */
}

/* 单页编程,len 不得跨 256B 页边界 */
static int page_program(uint32_t addr, const uint8_t *buf, uint16_t len)
{
    write_enable();
    CS_LOW();
    send_cmd_addr(CMD_PAGE_PROGRAM, addr);
    HAL_SPI_Transmit(&hspi1, (uint8_t *)buf, len, 100);
    CS_HIGH();
    return wait_busy(10);      /* 页编程最大 3ms */
}

int W25_Write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint16_t chunk;

    while (len > 0) {
        chunk = (uint16_t)(W25_PAGE_SIZE - (addr % W25_PAGE_SIZE));
        if (chunk > len) {
            chunk = (uint16_t)len;
        }
        if (page_program(addr, buf, chunk) != 0) {
            return -1;
        }
        addr += chunk;
        buf  += chunk;
        len  -= chunk;
    }
    return 0;
}
