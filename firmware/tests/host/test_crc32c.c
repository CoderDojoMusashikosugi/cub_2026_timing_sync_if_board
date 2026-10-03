/* UT-01 CRC32C */
#include "test_harness.h"
#include "ts_crc32c.h"

static void test_check_value(void)
{
    CHECK_EQ_HEX(ts_crc32c("123456789", 9), 0xE3069283u);
}

static void test_empty(void)
{
    CHECK_EQ_HEX(ts_crc32c("", 0), 0x00000000u);
}

/* RFC 3720 B.4（iSCSI）の試験vector。RFCは送信byte順で書いてあるため値はbyte逆順になる */
static void test_rfc3720_vectors(void)
{
    uint8_t buf[32];

    memset(buf, 0x00, sizeof(buf));
    CHECK_EQ_HEX(ts_crc32c(buf, sizeof(buf)), 0x8A9136AAu);

    memset(buf, 0xFF, sizeof(buf));
    CHECK_EQ_HEX(ts_crc32c(buf, sizeof(buf)), 0x62A8AB43u);

    for (unsigned i = 0; i < 32u; i++) {
        buf[i] = (uint8_t)i;
    }
    CHECK_EQ_HEX(ts_crc32c(buf, sizeof(buf)), 0x46DD794Eu);

    for (unsigned i = 0; i < 32u; i++) {
        buf[i] = (uint8_t)(31u - i);
    }
    CHECK_EQ_HEX(ts_crc32c(buf, sizeof(buf)), 0x113FDB5Cu);
}

static void test_split_equals_whole(void)
{
    const char *s = "The quick brown fox jumps over the lazy dog";
    const size_t n = strlen(s);
    const uint32_t whole = ts_crc32c(s, n);

    for (size_t cut = 0; cut <= n; cut++) {
        uint32_t st = ts_crc32c_update(TS_CRC32C_INIT, s, cut);
        st = ts_crc32c_update(st, s + cut, n - cut);
        CHECK_EQ_HEX(ts_crc32c_final(st), whole);
    }
}

int main(void)
{
    RUN_TEST(test_check_value);
    RUN_TEST(test_empty);
    RUN_TEST(test_rfc3720_vectors);
    RUN_TEST(test_split_equals_whole);
    TEST_EXIT();
}
