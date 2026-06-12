#ifndef __OTA_BOOT_H
#define __OTA_BOOT_H

/* Bootloader 端升级处理(仅 Bootloader 工程使用)
 * 在跳转 App 之前调用:
 *   - 发现"试运行未确认"标志 → 上次新固件没跑起来,从备份区回滚;
 *   - 发现"升级请求"标志   → 校验下载区 → 备份旧固件 → 搬运 → 校验 → 写试运行标志。
 * 处理完成后正常返回,由调用者继续执行跳转。
 */
void OTA_Boot(void);

#endif /* __OTA_BOOT_H */
