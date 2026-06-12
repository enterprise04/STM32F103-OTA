#include "crc32.h"

uint32_t crc32_calc(uint32_t crc, const uint8_t *buf, uint32_t len)
{
    uint32_t i;

    crc = ~crc;
    while (len--) {
        crc ^= *buf++;
        for (i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)(-(int32_t)(crc & 1U)));
        }
    }
    return ~crc;
}
