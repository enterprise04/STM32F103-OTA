#ifndef __OTA_H
#define __OTA_H

#include <stdint.h>
#include "w25q64.h"

/* ============ 串口帧协议(上位机 → 板) ============
 *
 *  | 0xAA | 0x55 | CMD(1) | SEQ(1) | LEN(2,小端) | DATA(LEN) | CRC32(4,小端) |
 *
 *  CRC32 覆盖 CMD..DATA(即帧的第 2 字节到 DATA 末尾),算法同 zlib.crc32。
 *  DATA 每包固定 1024 字节(末包可不足),保证写入始终对齐 4K 扇区。
 *
 *  板端应答固定 4 字节: | 0x55 | 0xAA | STATUS(1) | SEQ(1) |
 */
#define OTA_FRAME_H0        0xAA
#define OTA_FRAME_H1        0x55
#define OTA_DATA_MAX        1024U
#define OTA_FRAME_OVERHEAD  10U     /* 头2 + CMD1 + SEQ1 + LEN2 + CRC4 */

/* CMD */
#define OTA_CMD_START       0x01    /* DATA = fw_size(4) + fw_crc32(4) + version(4),均小端 */
#define OTA_CMD_DATA        0x02    /* DATA = 固件分片 */
#define OTA_CMD_END         0x03    /* DATA 空,板端校验整包 CRC 并写参数区 */

/* STATUS */
#define OTA_ST_OK           0x00
#define OTA_ST_ERR_CRC      0x01    /* 帧 CRC 错,请求重传 */
#define OTA_ST_ERR_SEQ      0x02    /* 序号不符 */
#define OTA_ST_ERR_STATE    0x03    /* 状态/参数非法(如未 START、固件超长) */
#define OTA_ST_ERR_FLASH    0x04    /* W25Q64 操作失败 */
#define OTA_ST_ERR_FWCRC    0x05    /* 整包固件 CRC 校验失败 */

/* ============ W25Q64 参数区布局(Boot 与 App 共用,每条记录独占 4K 扇区) ============ */
#define OTA_REQ_ADDR        (W25_PART_PARAM + 0x0000)   /* 升级请求(App 在 END 时写) */
#define OTA_BAK_ADDR        (W25_PART_PARAM + 0x1000)   /* 备份记录(Boot 备份完成后写) */
#define OTA_TRIAL_ADDR      (W25_PART_PARAM + 0x2000)   /* 试运行标志(Boot 写,App 确认后擦) */

#define OTA_MAGIC_UPDATE    0x5A5AA5A5UL    /* 有待升级固件 */
#define OTA_MAGIC_BACKUP    0xA5A55A5AUL    /* 备份区数据有效 */
#define OTA_MAGIC_TRIAL     0x5AA5A55AUL    /* 新固件试运行中,未被 App 确认 */

typedef struct {
    uint32_t magic;     /* OTA_MAGIC_UPDATE / OTA_MAGIC_TRIAL */
    uint32_t fw_size;   /* 固件字节数 */
    uint32_t fw_crc32;  /* 整包固件 CRC32 */
    uint32_t version;   /* 高16位主版本,低16位次版本 */
} ota_param_t;

typedef struct {
    uint32_t magic;     /* OTA_MAGIC_BACKUP */
    uint32_t fw_size;   /* 备份长度(整个 App 区) */
    uint32_t fw_crc32;  /* 备份数据 CRC32,回滚后校验用 */
    uint32_t session;   /* 升级会话标识(=新固件 CRC32):断电重来时凭它跳过重复备份,
                           防止把已半擦除的 App 区再备份一遍冲掉好备份 */
} ota_backup_t;

#endif /* __OTA_H */
