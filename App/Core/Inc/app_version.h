#ifndef __APP_VERSION_H
#define __APP_VERSION_H

#include <stdint.h>

/* ====== 全工程唯一需要改版本号的地方,发版前改这里并重新编译 ======
 * 上位机会从 bin 中自动读取版本,推送时无需再传 --version。
 */
#define APP_VERSION_MAJOR   3
#define APP_VERSION_MINOR   1

#define APP_VERSION_WORD    (((uint32_t)APP_VERSION_MAJOR << 16) | (uint32_t)APP_VERSION_MINOR)

/* 固件信息块:以连续双魔数开头,随固件编译进 bin 的任意位置,
 * 上位机按魔数字节序列扫描定位(见 Host/ota_host.py 的 embedded_version)。
 */
#define FW_INFO_MAGIC0      0x52565746UL    /* 小端存储后为 'FWVR' */
#define FW_INFO_MAGIC1      0x4F464E49UL    /* 小端存储后为 'INFO' */

typedef struct {
    uint32_t magic0;
    uint32_t magic1;
    uint32_t version;   /* 高16位主版本,低16位次版本 */
} fw_info_t;

/* volatile:阻止编译器把 const 已知值常量折叠,保证产生真实内存引用,
 * 否则对象无人引用,会被链接器的未使用段消除裁出镜像,bin 里就找不到魔数了 */
extern const volatile fw_info_t g_fw_info;

#endif /* __APP_VERSION_H */
