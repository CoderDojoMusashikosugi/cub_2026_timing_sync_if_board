/* UT-02 Jetson frame解析、UT-03 TIME_SETの検証、UT-04 応答のpack */
#include <stdbool.h>

#include "test_harness.h"
#include "ts_crc32c.h"
#include "ts_jetson_proto.h"
#include "ts_time.h"

/* ---- 受信frameの記録 ---- */

#define MAX_RECORDED 64

typedef struct {
    uint8_t command;
    uint16_t sequence;
    uint16_t payload_length;
    uint8_t payload[TS_JP_MAX_PAYLOAD];
    uint32_t rx_complete_ms;
} recorded_t;

typedef struct {
    recorded_t frames[MAX_RECORDED];
    size_t count;
    size_t total;
} recorder_t;

static void record_frame(void *ctx, const ts_jp_frame_t *f)
{
    recorder_t *r = (recorder_t *)ctx;

    r->total++;
    if (r->count < MAX_RECORDED) {
        recorded_t *dst = &r->frames[r->count++];
        dst->command = f->command;
        dst->sequence = f->sequence;
        dst->payload_length = f->payload_length;
        memcpy(dst->payload, f->payload, f->payload_length);
        dst->rx_complete_ms = f->rx_complete_ms;
    }
}

typedef struct {
    ts_jp_parser_t parser;
    recorder_t rec;
    uint32_t now;
} fixture_t;

static void fixture_init(fixture_t *fx)
{
    memset(&fx->rec, 0, sizeof(fx->rec));
    ts_jp_parser_init(&fx->parser, record_frame, &fx->rec);
    fx->now = 1000;
}

/* 1 msおきに1 byteずつ入れる */
static void feed(fixture_t *fx, const uint8_t *data, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        fx->now++;
        ts_jp_parser_push(&fx->parser, data[i], fx->now);
    }
}

static void idle(fixture_t *fx, uint32_t ms)
{
    fx->now += ms;
    ts_jp_parser_poll(&fx->parser, fx->now);
}

static size_t make_get_status(uint8_t *out, uint16_t seq)
{
    return ts_jp_encode_frame(out, TS_JP_MAX_FRAME, TS_JP_CMD_GET_STATUS, seq, NULL, 0);
}

static size_t make_set_time(uint8_t *out, uint16_t seq, int64_t unix_sec)
{
    const ts_jp_time_set_t t = {unix_sec, 123456789u, 1000u, 0xCAFEBABEu};
    uint8_t payload[TS_JP_TIME_SET_LEN];

    ts_jp_encode_time_set(&t, payload);
    return ts_jp_encode_frame(out, TS_JP_MAX_FRAME, TS_JP_CMD_SET_TIME, seq, payload, sizeof(payload));
}

/* headerを任意に作り、CRCを正しく付ける（version不正などの試験用） */
static size_t make_raw(uint8_t *out, uint8_t version, uint8_t cmd, uint16_t plen_field, const uint8_t *payload,
                       uint16_t plen_actual)
{
    out[0] = TS_JP_MAGIC0;
    out[1] = TS_JP_MAGIC1;
    out[2] = version;
    out[3] = cmd;
    out[4] = (uint8_t)plen_field;
    out[5] = (uint8_t)(plen_field >> 8);
    out[6] = 0x01;
    out[7] = 0x00;
    memcpy(&out[8], payload, plen_actual);
    const uint32_t crc = ts_crc32c(out, 8u + plen_actual);
    for (unsigned i = 0; i < 4u; i++) {
        out[8u + plen_actual + i] = (uint8_t)(crc >> (8u * i));
    }
    return 8u + plen_actual + 4u;
}

/* ---- frame層 ---- */

static void test_encode_layout(void)
{
    uint8_t f[TS_JP_MAX_FRAME];
    const size_t n = make_get_status(f, 0x1234);
    const uint8_t header[] = {0x54, 0x53, 0x01, 0x01, 0x00, 0x00, 0x34, 0x12};
    const uint32_t crc = ts_crc32c(f, 8);

    CHECK_EQ_INT(n, 12);
    CHECK_EQ_MEM(f, header, sizeof(header));
    CHECK_EQ_INT(f[8], crc & 0xFFu);
    CHECK_EQ_INT(f[11], crc >> 24);

    uint8_t payload[TS_JP_MAX_PAYLOAD + 1] = {0};
    CHECK(ts_jp_encode_frame(f, sizeof(f), 0x01, 0, payload, TS_JP_MAX_PAYLOAD) == 60u);
    CHECK(ts_jp_encode_frame(f, sizeof(f), 0x01, 0, payload, TS_JP_MAX_PAYLOAD + 1) == 0u);
    CHECK(ts_jp_encode_frame(f, 11, 0x01, 0, NULL, 0) == 0u);
}

static void test_receive_valid_frames(void)
{
    fixture_t fx;
    uint8_t f[TS_JP_MAX_FRAME];

    fixture_init(&fx);
    feed(&fx, f, make_get_status(f, 7));
    feed(&fx, f, make_set_time(f, 8, 1790000000));
    CHECK_EQ_INT(fx.rec.count, 2);
    CHECK_EQ_INT(fx.rec.frames[0].command, TS_JP_CMD_GET_STATUS);
    CHECK_EQ_INT(fx.rec.frames[0].sequence, 7);
    CHECK_EQ_INT(fx.rec.frames[0].payload_length, 0);
    CHECK_EQ_INT(fx.rec.frames[1].command, TS_JP_CMD_SET_TIME);
    CHECK_EQ_INT(fx.rec.frames[1].payload_length, TS_JP_TIME_SET_LEN);
    CHECK_EQ_INT(fx.rec.frames[1].rx_complete_ms, fx.now);
    CHECK_EQ_INT(fx.parser.stats.frames_ok, 2);
    CHECK_EQ_INT(fx.parser.stats.dropped_bytes, 0);
}

static void test_noise_is_skipped(void)
{
    fixture_t fx;
    uint8_t f[TS_JP_MAX_FRAME];
    const uint8_t noise[] = {0x00, 0xFF, 'T', 'x', 'T', 'T', 'S', 0x09};

    fixture_init(&fx);
    feed(&fx, noise, sizeof(noise));
    feed(&fx, f, make_get_status(f, 1));
    CHECK_EQ_INT(fx.rec.count, 1);
    CHECK_EQ_INT(fx.rec.frames[0].sequence, 1);
    CHECK_EQ_INT(fx.parser.stats.version_errors, 1); /* "TS"の後の0x09 */
}

static void test_interbyte_timeout(void)
{
    fixture_t fx;
    uint8_t f[TS_JP_MAX_FRAME];
    const size_t n = make_get_status(f, 3);

    /* 20 msちょうどの間隔は有効 */
    fixture_init(&fx);
    feed(&fx, f, 5);
    fx.now += 19; /* feed()が+1するので間隔20 ms */
    feed(&fx, f + 5, n - 5);
    CHECK_EQ_INT(fx.rec.count, 1);
    CHECK_EQ_INT(fx.parser.stats.timeouts, 0);

    /* 21 msは捨てる。続く正しいframeは受ける */
    fixture_init(&fx);
    feed(&fx, f, 5);
    fx.now += 20;
    feed(&fx, f + 5, n - 5);
    CHECK_EQ_INT(fx.rec.count, 0);
    CHECK_EQ_INT(fx.parser.stats.timeouts, 1);
    CHECK_EQ_HEX(fx.parser.stats.last_error, TS_JP_ERR_FRAME_TIMEOUT);
    idle(&fx, 50);
    feed(&fx, f, n);
    CHECK_EQ_INT(fx.rec.count, 1);

    /* byteが来ないままでもpollでtimeoutを判定する */
    fixture_init(&fx);
    feed(&fx, f, 5);
    idle(&fx, 20);
    CHECK_EQ_INT(fx.parser.stats.timeouts, 0);
    idle(&fx, 1);
    CHECK_EQ_INT(fx.parser.stats.timeouts, 1);
    CHECK_EQ_INT(fx.parser.len, 0);
}

static void test_header_errors_then_recover(void)
{
    fixture_t fx;
    uint8_t f[TS_JP_MAX_FRAME];
    uint8_t good[TS_JP_MAX_FRAME];
    const uint8_t zeros[TS_JP_MAX_PAYLOAD] = {0};
    const size_t good_n = make_get_status(good, 42);

    fixture_init(&fx);
    feed(&fx, f, make_raw(f, 0x02, TS_JP_CMD_GET_STATUS, 0, zeros, 0)); /* version不正 */
    feed(&fx, good, good_n);
    CHECK_EQ_INT(fx.parser.stats.version_errors, 1);
    CHECK_EQ_INT(fx.rec.count, 1);
    CHECK_EQ_INT(fx.rec.frames[0].sequence, 42);

    fixture_init(&fx);
    feed(&fx, f, make_raw(f, TS_JP_VERSION, TS_JP_CMD_GET_STATUS, TS_JP_MAX_PAYLOAD + 1, zeros, 0)); /* length不正 */
    feed(&fx, good, good_n);
    CHECK_EQ_INT(fx.parser.stats.length_errors, 1);
    CHECK_EQ_INT(fx.rec.count, 1);

    fixture_init(&fx);
    memcpy(f, good, good_n);
    f[good_n - 1] ^= 0x01; /* CRC不一致 */
    feed(&fx, f, good_n);
    feed(&fx, good, good_n);
    CHECK_EQ_INT(fx.parser.stats.crc_errors, 1);
    CHECK_EQ_INT(fx.rec.count, 1);
}

static void test_consecutive_errors(void)
{
    fixture_t fx;
    uint8_t good[TS_JP_MAX_FRAME];
    uint8_t bad[TS_JP_MAX_FRAME];
    const size_t n = make_get_status(good, 5);

    memcpy(bad, good, n);
    bad[9] ^= 0x80;
    fixture_init(&fx);
    for (int i = 0; i < 3; i++) {
        feed(&fx, bad, n);
    }
    CHECK_EQ_INT(fx.parser.stats.consecutive_errors, 3);
    CHECK_EQ_HEX(fx.parser.stats.last_error, TS_JP_ERR_FRAME_CRC);
    feed(&fx, good, n);
    CHECK_EQ_INT(fx.parser.stats.consecutive_errors, 0);
    CHECK_EQ_INT(fx.rec.count, 1);
}

/* 有効frameの全bit位置を1つずつ反転させても、そのframeは受理されない */
static void test_every_single_bit_flip(void)
{
    uint8_t good[TS_JP_MAX_FRAME];
    uint8_t probe[TS_JP_MAX_FRAME];
    const size_t n = make_set_time(good, 100, 1790000000);
    const size_t probe_n = make_get_status(probe, 200);
    int accepted_corrupt = 0;
    int lost_probe = 0;

    for (size_t bit = 0; bit < n * 8u; bit++) {
        fixture_t fx;
        uint8_t f[TS_JP_MAX_FRAME];

        memcpy(f, good, n);
        f[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        fixture_init(&fx);
        feed(&fx, f, n);
        idle(&fx, 30);
        feed(&fx, probe, probe_n);
        for (size_t i = 0; i < fx.rec.count; i++) {
            if (fx.rec.frames[i].command != TS_JP_CMD_GET_STATUS) {
                accepted_corrupt++;
            }
        }
        if (fx.rec.count != 1u || fx.rec.frames[0].sequence != 200u) {
            lost_probe++;
        }
    }
    CHECK_EQ_INT(accepted_corrupt, 0);
    CHECK_EQ_INT(lost_probe, 0);
}

/* frame内部でのbyte欠落・追加は受理されない */
static void test_byte_drop_and_insert(void)
{
    uint8_t good[TS_JP_MAX_FRAME];
    const size_t n = make_set_time(good, 100, 1790000000);
    int accepted = 0;

    for (size_t pos = 0; pos < n; pos++) {
        fixture_t fx;
        uint8_t f[TS_JP_MAX_FRAME + 1];

        /* 欠落 */
        memcpy(f, good, pos);
        memcpy(f + pos, good + pos + 1, n - pos - 1);
        fixture_init(&fx);
        feed(&fx, f, n - 1);
        idle(&fx, 30);
        accepted += (int)fx.rec.total;

        /* frame内部への追加 */
        if (pos > 0) {
            for (unsigned v = 0; v < 256u; v += 37u) {
                memcpy(f, good, pos);
                f[pos] = (uint8_t)v;
                memcpy(f + pos + 1, good + pos, n - pos);
                fixture_init(&fx);
                feed(&fx, f, n + 1);
                idle(&fx, 30);
                accepted += (int)fx.rec.total;
            }
        }
    }
    CHECK_EQ_INT(accepted, 0);
}

/* 偽のheaderの中に埋もれた正しいframeを、CRC errorまたはtimeoutの後の再走査で拾う */
static void test_resync_finds_embedded_frame(void)
{
    uint8_t good[TS_JP_MAX_FRAME];
    const size_t good_n = make_get_status(good, 9);
    const uint8_t bogus_header[] = {'T', 'S', 0x01, 0x01, 16, 0x00, 0x00, 0x00}; /* payload 16 byteを主張 */
    fixture_t fx;

    /* 偽frameの長さ（28 byte）に達したところでCRC errorになり、再走査で拾う */
    fixture_init(&fx);
    feed(&fx, bogus_header, sizeof(bogus_header));
    feed(&fx, good, good_n);
    const uint32_t good_end = fx.now;
    const uint8_t filler[8] = {0};
    feed(&fx, filler, sizeof(filler));
    CHECK_EQ_INT(fx.parser.stats.crc_errors, 1);
    CHECK_EQ_INT(fx.rec.count, 1);
    CHECK_EQ_INT(fx.rec.frames[0].sequence, 9);
    CHECK_EQ_INT(fx.rec.frames[0].rx_complete_ms, good_end); /* 受信完了時刻は埋もれていたframeの最後のbyte */

    /* 偽frameが未完成のまま止まった場合は、timeoutの後の再走査で拾う */
    fixture_init(&fx);
    feed(&fx, bogus_header, sizeof(bogus_header));
    feed(&fx, good, good_n);
    const uint32_t good_end2 = fx.now;
    CHECK_EQ_INT(fx.rec.count, 0);
    idle(&fx, 21);
    CHECK_EQ_INT(fx.parser.stats.timeouts, 1);
    CHECK_EQ_INT(fx.rec.count, 1);
    CHECK_EQ_INT(fx.rec.frames[0].rx_complete_ms, good_end2);
    CHECK_EQ_INT(fx.parser.len, 0);
}

static uint32_t xorshift32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

/* random byte列の間に正しいframeを混ぜる。正しいframeはすべて拾い、それ以外は受理しない */
static void test_random_noise_fuzz(void)
{
    fixture_t fx;
    uint32_t seed = 0x12345678u;
    uint32_t inserted = 0;
    uint32_t matched = 0;
    uint32_t foreign = 0;
    size_t checked = 0;

    fixture_init(&fx);
    for (int round = 0; round < 20000; round++) {
        uint8_t noise[64];
        uint8_t f[TS_JP_MAX_FRAME];
        const size_t noise_n = xorshift32(&seed) % sizeof(noise);

        for (size_t i = 0; i < noise_n; i++) {
            const uint32_t r = xorshift32(&seed);
            /* magicやversionに似たbyteを多めに混ぜる */
            noise[i] = (r & 0x300u) == 0u ? (uint8_t)"TS\x01"[r % 3u] : (uint8_t)r;
        }
        feed(&fx, noise, noise_n);
        feed(&fx, f, make_get_status(f, (uint16_t)inserted));
        inserted++;

        /* 記録bufferが溢れる前に照合する */
        if (fx.rec.count > MAX_RECORDED / 2) {
            for (; checked < fx.rec.count; checked++) {
                if (fx.rec.frames[checked].command == TS_JP_CMD_GET_STATUS &&
                    fx.rec.frames[checked].payload_length == 0u) {
                    matched++;
                } else {
                    foreign++;
                }
            }
            fx.rec.count = 0;
            checked = 0;
        }
    }
    idle(&fx, 100);
    for (; checked < fx.rec.count; checked++) {
        if (fx.rec.frames[checked].command == TS_JP_CMD_GET_STATUS && fx.rec.frames[checked].payload_length == 0u) {
            matched++;
        } else {
            foreign++;
        }
    }
    CHECK_EQ_INT(foreign, 0);
    CHECK_EQ_INT(matched, inserted);
}

/* ---- TIME_SET ---- */

static ts_jp_result_t decode_with(int64_t unix_sec, uint32_t ns, uint32_t valid_ms, ts_jp_time_set_t *out)
{
    const ts_jp_time_set_t t = {unix_sec, ns, valid_ms, 1u};
    uint8_t p[TS_JP_TIME_SET_LEN];

    ts_jp_encode_time_set(&t, p);
    return ts_jp_decode_time_set(p, sizeof(p), out);
}

static void test_time_set_codec(void)
{
    ts_jp_time_set_t out;
    const ts_jp_time_set_t in = {1790000000, 999999999u, 5000u, 0xDEADBEEFu};
    uint8_t p[TS_JP_TIME_SET_LEN + 1] = {0};

    ts_jp_encode_time_set(&in, p);
    CHECK_EQ_INT(ts_jp_decode_time_set(p, TS_JP_TIME_SET_LEN, &out), TS_JP_RESULT_OK);
    CHECK_EQ_INT(out.unix_sec, in.unix_sec);
    CHECK_EQ_INT(out.nanoseconds, in.nanoseconds);
    CHECK_EQ_INT(out.valid_for_ms, in.valid_for_ms);
    CHECK_EQ_HEX(out.source_id, in.source_id);
    /* little-endian */
    CHECK_EQ_HEX(p[0], 1790000000u & 0xFFu);
    CHECK_EQ_HEX(p[16], 0xEF);

    CHECK_EQ_INT(ts_jp_decode_time_set(p, TS_JP_TIME_SET_LEN - 1, &out), TS_JP_NACK_BAD_LENGTH);
    CHECK_EQ_INT(ts_jp_decode_time_set(p, TS_JP_TIME_SET_LEN + 1, &out), TS_JP_NACK_BAD_LENGTH);
}

static void test_time_set_ranges(void)
{
    ts_jp_time_set_t out;

    CHECK_EQ_INT(decode_with(1790000000, 999999999u, 1000u, &out), TS_JP_RESULT_OK);
    CHECK_EQ_INT(decode_with(1790000000, 1000000000u, 1000u, &out), TS_JP_NACK_OUT_OF_RANGE);
    CHECK_EQ_INT(decode_with(1790000000, 0u, 0u, &out), TS_JP_NACK_OUT_OF_RANGE);
    CHECK_EQ_INT(decode_with(1790000000, 0u, 1u, &out), TS_JP_RESULT_OK);
    CHECK_EQ_INT(decode_with(1790000000, 0u, 5000u, &out), TS_JP_RESULT_OK);
    CHECK_EQ_INT(decode_with(1790000000, 0u, 5001u, &out), TS_JP_NACK_OUT_OF_RANGE);
    CHECK_EQ_INT(decode_with(TS_RTC_MIN_UNIX - 1, 0u, 1000u, &out), TS_JP_NACK_OUT_OF_RANGE);
    CHECK_EQ_INT(decode_with(TS_RTC_MIN_UNIX, 0u, 1000u, &out), TS_JP_RESULT_OK);
    CHECK_EQ_INT(decode_with(TS_RTC_MAX_UNIX, 0u, 1000u, &out), TS_JP_RESULT_OK);
    CHECK_EQ_INT(decode_with(TS_RTC_MAX_UNIX + 1, 0u, 1000u, &out), TS_JP_NACK_OUT_OF_RANGE);
    CHECK_EQ_INT(decode_with(-1, 0u, 1000u, &out), TS_JP_NACK_OUT_OF_RANGE);
    TODO("P-17: SETで受け付けるUnix秒の範囲をRTC範囲（2000～2099年）より狭めるか未決定");
}

static void test_time_set_expiry(void)
{
    CHECK(!ts_jp_time_set_expired(1000u, 100u, 1100u));
    CHECK(ts_jp_time_set_expired(1000u, 100u, 1101u));
    CHECK(!ts_jp_time_set_expired(0xFFFFFFF0u, 100u, 0x00000050u)); /* tick wrap、経過96 ms */
    CHECK(ts_jp_time_set_expired(0xFFFFFFF0u, 100u, 0x00000060u));  /* 経過112 ms */
}

/* ---- 応答 ---- */

static void test_time_status_pack(void)
{
    const ts_jp_time_status_t in = {
        .unix_sec = 0x0102030405060708LL,
        .nanoseconds = 0x11223344u,
        .state = TS_JP_STATE_TCXO_HOLDOVER,
        .flags = 0xFF, /* 未定義bitは0にして送る */
        .last_source_id = 0xA1A2A3A4u,
        .last_sequence = 0xB1B2u,
        .last_error = TS_JP_ERR_FRAME_CRC,
    };
    const uint8_t expected[TS_JP_TIME_STATUS_LEN] = {
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, /* Unix seconds */
        0x44, 0x33, 0x22, 0x11,                         /* nanoseconds */
        0x04,                                           /* state */
        0x17,                                           /* flags: bit0,1,2,4 */
        0x00, 0x00,                                     /* reserved */
        0xA4, 0xA3, 0xA2, 0xA1,                         /* last source_id */
        0xB2, 0xB1,                                     /* last sequence */
        0x00, 0x00,                                     /* clock diagnostic（DR-050で0固定） */
        0x01, 0x01,                                     /* last error */
    };
    uint8_t out[TS_JP_TIME_STATUS_LEN];
    ts_jp_time_status_t back;

    ts_jp_pack_time_status(&in, out);
    CHECK_EQ_MEM(out, expected, sizeof(expected));
    ts_jp_unpack_time_status(out, &back);
    CHECK_EQ_INT(back.unix_sec, in.unix_sec);
    CHECK_EQ_INT(back.flags, 0x17);
    CHECK_EQ_INT(back.last_error, in.last_error);
}

static void test_ack_pack(void)
{
    uint8_t out[TS_JP_ACK_LEN];

    ts_jp_pack_ack(TS_JP_CMD_SET_TIME, TS_JP_NACK_GNSS_ACTIVE, out);
    CHECK_EQ_INT(out[0], TS_JP_CMD_SET_TIME);
    CHECK_EQ_INT(out[1], TS_JP_NACK_GNSS_ACTIVE);
    TODO("P-12: 大offset拒否時に現在値・要求値・offsetを返すか（NACK payloadは2 bytes）未決定");
}

/* ---- 再送 ---- */

static void test_dedup(void)
{
    ts_jp_dedup_t d;
    const uint8_t payload_a[4] = {1, 2, 3, 4};
    const uint8_t payload_b[4] = {1, 2, 3, 5};
    const uint8_t response[] = {0xAA, 0xBB, 0xCC};
    ts_jp_frame_t f = {TS_JP_CMD_SET_TIME, 10, 4, payload_a, 0};

    ts_jp_dedup_init(&d);
    CHECK_EQ_INT(ts_jp_dedup_check(&d, &f), TS_JP_DEDUP_NEW);
    ts_jp_dedup_store(&d, &f, response, sizeof(response));
    CHECK_EQ_INT(ts_jp_dedup_check(&d, &f), TS_JP_DEDUP_REPLAY);
    CHECK_EQ_INT(d.response_length, sizeof(response));
    CHECK_EQ_MEM(d.response, response, sizeof(response));

    ts_jp_frame_t other_seq = f;
    other_seq.sequence = 11;
    CHECK_EQ_INT(ts_jp_dedup_check(&d, &other_seq), TS_JP_DEDUP_NEW);

    ts_jp_frame_t other_payload = f;
    other_payload.payload = payload_b;
    CHECK_EQ_INT(ts_jp_dedup_check(&d, &other_payload), TS_JP_DEDUP_CONFLICT);

    ts_jp_frame_t other_cmd = f;
    other_cmd.command = TS_JP_CMD_FORCE_SET_TIME;
    CHECK_EQ_INT(ts_jp_dedup_check(&d, &other_cmd), TS_JP_DEDUP_CONFLICT);
    TODO("P-12: 同じsequenceで内容が違うrequest（CONFLICT）の扱いが未決定");
}

int main(void)
{
    RUN_TEST(test_encode_layout);
    RUN_TEST(test_receive_valid_frames);
    RUN_TEST(test_noise_is_skipped);
    RUN_TEST(test_interbyte_timeout);
    RUN_TEST(test_header_errors_then_recover);
    RUN_TEST(test_consecutive_errors);
    RUN_TEST(test_every_single_bit_flip);
    RUN_TEST(test_byte_drop_and_insert);
    RUN_TEST(test_resync_finds_embedded_frame);
    RUN_TEST(test_random_noise_fuzz);
    RUN_TEST(test_time_set_codec);
    RUN_TEST(test_time_set_ranges);
    RUN_TEST(test_time_set_expiry);
    RUN_TEST(test_time_status_pack);
    RUN_TEST(test_ack_pack);
    RUN_TEST(test_dedup);
    TEST_EXIT();
}
