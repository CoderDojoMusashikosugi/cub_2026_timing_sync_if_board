/*
 * CRC32C（Castagnoli）
 *
 * reflected polynomial 0x82F63B78、init/final XOR 0xFFFFFFFF。
 * Jetson frame（jetson_time_protocol.md）とbackup payload CRCで共用する。
 */
#ifndef TS_CRC32C_H
#define TS_CRC32C_H

#include <stddef.h>
#include <stdint.h>

#define TS_CRC32C_INIT 0xFFFFFFFFu

/* 途中経過（final XOR前の値）を更新する。分割して計算するときに使う。 */
uint32_t ts_crc32c_update(uint32_t state, const void *data, size_t len);

static inline uint32_t ts_crc32c_final(uint32_t state)
{
    return state ^ 0xFFFFFFFFu;
}

/* 一括計算 */
uint32_t ts_crc32c(const void *data, size_t len);

#endif /* TS_CRC32C_H */
