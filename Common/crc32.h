#ifndef __CRC32_H
#define __CRC32_H

#include <stdint.h>

/* 标准 CRC32(反射,多项式 0xEDB88320),与 Python zlib.crc32 结果一致。
 * 首次调用 crc 传 0;分段计算时把上次返回值继续传入。
 */
uint32_t crc32_calc(uint32_t crc, const uint8_t *buf, uint32_t len);

#endif /* __CRC32_H */
