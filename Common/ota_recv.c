#include <stdio.h>
#include <string.h>
#include "ota_recv.h"
#include "ota.h"
#include "w25q64.h"
#include "crc32.h"
#include "boot.h"          /* APP_MAX_SIZE */
#include "sha256.h"
#include "micro-ecc/uECC.h"
#include "ecc_pubkey.h"    /* g_ecc_pubkey,由 Host/keygen.py 生成 */

extern UART_HandleTypeDef huart1;

static uint8_t  rx_buf[OTA_DATA_MAX + OTA_FRAME_OVERHEAD + 6];
static volatile uint16_t rx_len = 0;    /* >0:有一帧待主循环处理 */

/* 升级会话上下文 */
static struct {
    uint8_t  active;
    uint32_t fw_size;
    uint32_t fw_crc32;
    uint32_t version;
    uint32_t offset;        /* 已写入下载区的字节数 */
    uint8_t  next_seq;
    uint8_t  sig[OTA_SIG_LEN];   /* 本次固件的 ECDSA 签名(START 帧携带) */
} ctx;

/* 重新武装一次 DMA 空闲接收 */
static void arm_rx(void)
{
    rx_len = 0;
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_buf, sizeof(rx_buf));
    /* 关掉 DMA 半满中断,否则一帧会触发两次回调 */
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART1) {
        rx_len = Size;      /* 只做标记,擦写等耗时操作放主循环 */
    }
}

void OTA_RecvInit(void)
{
    memset((void *)&ctx, 0, sizeof(ctx));
    arm_rx();
}

static void respond(uint8_t status, uint8_t seq)
{
    uint8_t r[4] = { 0x55, 0xAA, status, seq };
    HAL_UART_Transmit(&huart1, r, sizeof(r), 100);
}

/* 计算 W25Q64 下载区中固件的整包 CRC32 */
static uint32_t download_crc(uint32_t size)
{
    static uint8_t tmp[256];
    uint32_t crc = 0, off = 0, n;

    while (off < size) {
        n = size - off;
        if (n > sizeof(tmp)) {
            n = sizeof(tmp);
        }
        W25_Read(W25_PART_DOWNLOAD + off, tmp, n);
        crc = crc32_calc(crc, tmp, n);
        off += n;
    }
    return crc;
}

/* 计算 W25Q64 下载区中固件的 SHA-256 */
static void download_sha256(uint32_t size, uint8_t out[32])
{
    static uint8_t tmp[256];
    sha256_ctx c;
    uint32_t off = 0, n;

    sha256_init(&c);
    while (off < size) {
        n = size - off;
        if (n > sizeof(tmp)) {
            n = sizeof(tmp);
        }
        W25_Read(W25_PART_DOWNLOAD + off, tmp, n);
        sha256_update(&c, tmp, n);
        off += n;
    }
    sha256_final(&c, out);
}

static uint8_t handle_start(const uint8_t *data, uint16_t len)
{
    uint32_t size, crc, ver;

    if (len != 12U + OTA_SIG_LEN) {
        return OTA_ST_ERR_STATE;
    }
    memcpy(&size, data, 4);
    memcpy(&crc,  data + 4, 4);
    memcpy(&ver,  data + 8, 4);

    if (size == 0 || size > APP_MAX_SIZE) {
        return OTA_ST_ERR_STATE;
    }

    ctx.active   = 1;
    ctx.fw_size  = size;
    ctx.fw_crc32 = crc;
    ctx.version  = ver;
    ctx.offset   = 0;
    ctx.next_seq = 1;
    memcpy(ctx.sig, data + 12, OTA_SIG_LEN);
    return OTA_ST_OK;
}

static uint8_t handle_data(uint8_t seq, const uint8_t *data, uint16_t len)
{
    if (!ctx.active) {
        return OTA_ST_ERR_STATE;
    }
    /* 上位机没收到 ACK 而重发上一包:直接再次确认,不重复写 */
    if (seq == (uint8_t)(ctx.next_seq - 1U)) {
        return OTA_ST_OK;
    }
    if (seq != ctx.next_seq) {
        return OTA_ST_ERR_SEQ;
    }
    if (ctx.offset + len > ctx.fw_size) {
        return OTA_ST_ERR_STATE;
    }

    /* 跨入新 4K 扇区前先擦除(包长 1024,扇区边界必然对齐) */
    if ((ctx.offset % W25_SECTOR_SIZE) == 0) {
        if (W25_EraseSector(W25_PART_DOWNLOAD + ctx.offset) != 0) {
            return OTA_ST_ERR_FLASH;
        }
    }
    if (W25_Write(W25_PART_DOWNLOAD + ctx.offset, data, len) != 0) {
        return OTA_ST_ERR_FLASH;
    }

    ctx.offset += len;
    ctx.next_seq++;
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);    /* 每包闪一下,可视化进度 */
    return OTA_ST_OK;
}

static uint8_t handle_end(void)
{
    ota_param_t param;
    uint8_t     hash[32];

    if (!ctx.active || ctx.offset != ctx.fw_size) {
        return OTA_ST_ERR_STATE;
    }
    if (download_crc(ctx.fw_size) != ctx.fw_crc32) {
        return OTA_ST_ERR_FWCRC;
    }

    /* 数字签名验证:固件 SHA-256 必须由持有私钥者签名,否则拒绝 */
    download_sha256(ctx.fw_size, hash);
    if (!uECC_verify(g_ecc_pubkey, hash, sizeof(hash), ctx.sig, uECC_secp256r1())) {
        return OTA_ST_ERR_SIGN;
    }

    param.magic    = OTA_MAGIC_UPDATE;
    param.fw_size  = ctx.fw_size;
    param.fw_crc32 = ctx.fw_crc32;
    param.version  = ctx.version;

    if (W25_EraseSector(OTA_REQ_ADDR) != 0 ||
        W25_Write(OTA_REQ_ADDR, (uint8_t *)&param, sizeof(param)) != 0) {
        return OTA_ST_ERR_FLASH;
    }

    ctx.active = 0;
    return OTA_ST_OK;
}

void OTA_ConfirmBoot(void)
{
    ota_param_t trial;

    W25_Read(OTA_TRIAL_ADDR, (uint8_t *)&trial, sizeof(trial));
    if (trial.magic == OTA_MAGIC_TRIAL) {
        W25_EraseSector(OTA_TRIAL_ADDR);
        printf("[App] Trial boot confirmed (v%lu.%lu).\r\n",
               (unsigned long)(trial.version >> 16),
               (unsigned long)(trial.version & 0xFFFF));
    }
}

void OTA_RecvPoll(void)
{
    uint16_t len;
    uint8_t  cmd, seq, status;
    uint32_t crc_recv, crc_calc;
    uint8_t  end_ok = 0;

    if (rx_len == 0) {
        return;
    }

    /* 帧完整性检查:帧头、长度、CRC */
    len = (uint16_t)(rx_buf[4] | ((uint16_t)rx_buf[5] << 8));
    if (rx_len < OTA_FRAME_OVERHEAD ||
        rx_buf[0] != OTA_FRAME_H0 || rx_buf[1] != OTA_FRAME_H1 ||
        len > OTA_DATA_MAX || rx_len != OTA_FRAME_OVERHEAD + len) {
        arm_rx();                       /* 残帧直接丢弃,等上位机超时重发 */
        return;
    }

    cmd = rx_buf[2];
    seq = rx_buf[3];
    memcpy(&crc_recv, &rx_buf[6 + len], 4);
    crc_calc = crc32_calc(0, &rx_buf[2], 4U + len);

    if (crc_calc != crc_recv) {
        status = OTA_ST_ERR_CRC;
    } else {
        switch (cmd) {
        case OTA_CMD_START:
            status = handle_start(&rx_buf[6], len);
            break;
        case OTA_CMD_DATA:
            status = handle_data(seq, &rx_buf[6], len);
            break;
        case OTA_CMD_END:
            status = handle_end();
            end_ok = (status == OTA_ST_OK);
            break;
        default:
            status = OTA_ST_ERR_STATE;
            break;
        }
    }

    respond(status, seq);
    arm_rx();

    if (end_ok) {
        /* 传输期间不 printf(避免污染协议通道),结束后才打印 */
        printf("\r\n[OTA] FW v%lu.%lu received: %lu bytes, CRC+Sign OK. Param saved.\r\n",
               (unsigned long)(ctx.version >> 16),
               (unsigned long)(ctx.version & 0xFFFF),
               (unsigned long)ctx.fw_size);
        printf("[OTA] Reboot to apply (stage 4).\r\n");
    }
}
