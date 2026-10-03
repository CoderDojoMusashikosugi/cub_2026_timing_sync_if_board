#include "ts_crc32c.h"

#define CRC32C_POLY_REFLECTED 0x82F63B78u

/* 対象はJetson frame（最大64 byte）とbackup payloadだけなので、表を持たないbit単位計算で十分である。 */
uint32_t ts_crc32c_update(uint32_t state, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;

    for (size_t i = 0; i < len; i++) {
        state ^= p[i];
        for (unsigned bit = 0; bit < 8u; bit++) {
            state = (state & 1u) ? (state >> 1) ^ CRC32C_POLY_REFLECTED : state >> 1;
        }
    }
    return state;
}

uint32_t ts_crc32c(const void *data, size_t len)
{
    return ts_crc32c_final(ts_crc32c_update(TS_CRC32C_INIT, data, len));
}
