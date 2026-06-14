#include <stdio.h>
#include <string.h>
#include "ota_boot.h"
#include "ota.h"
#include "w25q64.h"
#include "crc32.h"
#include "stm_flash.h"
#include "boot.h"

/* 计算 W25Q64 中一段数据的 CRC32 */
static uint32_t w25_crc(uint32_t base, uint32_t size)
{
    static uint8_t tmp[256];
    uint32_t crc = 0, off = 0, n;

    while (off < size) {
        n = size - off;
        if (n > sizeof(tmp)) {
            n = sizeof(tmp);
        }
        W25_Read(base + off, tmp, n);
        crc = crc32_calc(crc, tmp, n);
        off += n;
    }
    return crc;
}

/* 把 W25Q64 中 size 字节固件搬进内部 Flash App 区并校验 */
static int flash_from_w25(uint32_t src, uint32_t size, uint32_t expect_crc)
{
    static uint8_t buf[1024];
    uint32_t off = 0, n;

    if (STM_EraseRegion(APP_ADDR, size) != 0) {
        return -1;
    }
    while (off < size) {
        n = size - off;
        if (n > sizeof(buf)) {
            n = sizeof(buf);
        }
        OTA_FEED_WDG();
        W25_Read(src + off, buf, n);
        if (STM_Write(APP_ADDR + off, buf, n) != 0) {
            return -1;
        }
        off += n;
    }
    /* 内部 Flash 可直接按地址读,整段再算一次 CRC */
    return (crc32_calc(0, (const uint8_t *)APP_ADDR, size) == expect_crc) ? 0 : -1;
}

/* 把当前 App 区整体备份到 W25Q64 备份区,session = 本次新固件 CRC */
static int do_backup(uint32_t session)
{
    ota_backup_t bak;
    uint32_t off;

    printf("[Boot] Backing up current App ...\r\n");
    for (off = 0; off < APP_MAX_SIZE; off += W25_SECTOR_SIZE) {
        OTA_FEED_WDG();
        if (W25_EraseSector(W25_PART_BACKUP + off) != 0) {
            return -1;
        }
    }
    for (off = 0; off < APP_MAX_SIZE; off += 1024U) {
        OTA_FEED_WDG();
        if (W25_Write(W25_PART_BACKUP + off,
                      (const uint8_t *)(APP_ADDR + off), 1024U) != 0) {
            return -1;
        }
    }

    bak.magic    = OTA_MAGIC_BACKUP;
    bak.fw_size  = APP_MAX_SIZE;
    bak.fw_crc32 = crc32_calc(0, (const uint8_t *)APP_ADDR, APP_MAX_SIZE);
    bak.session  = session;
    if (W25_EraseSector(OTA_BAK_ADDR) != 0 ||
        W25_Write(OTA_BAK_ADDR, (uint8_t *)&bak, sizeof(bak)) != 0) {
        return -1;
    }
    return 0;
}

/* 从备份区恢复旧固件 */
static void rollback(void)
{
    ota_backup_t bak;

    W25_Read(OTA_BAK_ADDR, (uint8_t *)&bak, sizeof(bak));
    if (bak.magic != OTA_MAGIC_BACKUP) {
        printf("[Boot] No backup available, rollback skipped.\r\n");
        W25_EraseSector(OTA_TRIAL_ADDR);
        return;
    }

    printf("[Boot] Rolling back from backup ...\r\n");
    if (flash_from_w25(W25_PART_BACKUP, bak.fw_size, bak.fw_crc32) == 0) {
        printf("[Boot] Rollback OK.\r\n");
    } else {
        printf("[Boot] Rollback FAILED! Device may be bricked, use ST-Link.\r\n");
    }
    W25_EraseSector(OTA_TRIAL_ADDR);
    W25_EraseSector(OTA_REQ_ADDR);      /* 未确认期间的升级请求一并作废 */
}

static void upgrade(const ota_param_t *req)
{
    ota_backup_t bak;
    ota_param_t  trial;

    printf("[Boot] Update request: v%lu.%lu, %lu bytes\r\n",
           (unsigned long)(req->version >> 16),
           (unsigned long)(req->version & 0xFFFF),
           (unsigned long)req->fw_size);

    if (req->fw_size == 0 || req->fw_size > APP_MAX_SIZE ||
        w25_crc(W25_PART_DOWNLOAD, req->fw_size) != req->fw_crc32) {
        printf("[Boot] Download image corrupted, update aborted.\r\n");
        W25_EraseSector(OTA_REQ_ADDR);
        return;
    }

    /* 断电重来时备份可能已做过(session 匹配),跳过以免备份到半擦除的 App */
    W25_Read(OTA_BAK_ADDR, (uint8_t *)&bak, sizeof(bak));
    if (bak.magic != OTA_MAGIC_BACKUP || bak.session != req->fw_crc32) {
        if (do_backup(req->fw_crc32) != 0) {
            printf("[Boot] Backup failed, update aborted.\r\n");
            return;                     /* 保留请求标志,下次复位重试 */
        }
    }

    printf("[Boot] Flashing new firmware ...\r\n");
    if (flash_from_w25(W25_PART_DOWNLOAD, req->fw_size, req->fw_crc32) == 0) {
        trial          = *req;
        trial.magic    = OTA_MAGIC_TRIAL;
        W25_EraseSector(OTA_REQ_ADDR);
        if (W25_EraseSector(OTA_TRIAL_ADDR) == 0) {
            W25_Write(OTA_TRIAL_ADDR, (uint8_t *)&trial, sizeof(trial));
        }
        printf("[Boot] Update OK, trial boot. App must confirm.\r\n");
    } else {
        printf("[Boot] Flash/verify failed!\r\n");
        rollback();
    }
}

void OTA_Boot(void)
{
    ota_param_t req, trial;

    if (W25_Init() != 0) {
        printf("[Boot] W25Q64 not found, OTA check skipped.\r\n");
        return;
    }

    W25_Read(OTA_TRIAL_ADDR, (uint8_t *)&trial, sizeof(trial));
    W25_Read(OTA_REQ_ADDR,   (uint8_t *)&req,   sizeof(req));

    if (trial.magic == OTA_MAGIC_TRIAL) {
        /* 上次升级后 App 从未确认(跑飞/复位),判定为坏固件 */
        printf("[Boot] Last FW v%lu.%lu was NOT confirmed.\r\n",
               (unsigned long)(trial.version >> 16),
               (unsigned long)(trial.version & 0xFFFF));
        rollback();
    } else if (req.magic == OTA_MAGIC_UPDATE) {
        upgrade(&req);
    }
}
