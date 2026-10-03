#include "ts_jetson_proto.h"

#include <string.h>

#include "ts_crc32c.h"
#include "ts_time.h"

/* TIME_SET payloadのoffset */
#define SET_OFF_UNIX 0u
#define SET_OFF_NS 8u
#define SET_OFF_VALID 12u
#define SET_OFF_SOURCE 16u
_Static_assert(SET_OFF_SOURCE + 4u == TS_JP_TIME_SET_LEN, "TIME_SET payload size");

/* TIME_STATUS payloadのoffset */
#define ST_OFF_UNIX 0u
#define ST_OFF_NS 8u
#define ST_OFF_STATE 12u
#define ST_OFF_FLAGS 13u
#define ST_OFF_RESERVED 14u
#define ST_OFF_SOURCE 16u
#define ST_OFF_SEQUENCE 20u
#define ST_OFF_CLOCK_DIAG 22u
#define ST_OFF_LAST_ERROR 24u
_Static_assert(ST_OFF_LAST_ERROR + 2u == TS_JP_TIME_STATUS_LEN, "TIME_STATUS payload size");

_Static_assert(TS_JP_HEADER_LEN + TS_JP_MAX_PAYLOAD + TS_JP_CRC_LEN <= TS_JP_MAX_FRAME, "frame size");

/* ---- little-endian ---- */

static void put_u16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32le(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4u; i++) {
        p[i] = (uint8_t)(v >> (8u * i));
    }
}

static void put_u64le(uint8_t *p, uint64_t v)
{
    for (unsigned i = 0; i < 8u; i++) {
        p[i] = (uint8_t)(v >> (8u * i));
    }
}

static uint16_t get_u16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_u32le(const uint8_t *p)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < 4u; i++) {
        v |= (uint32_t)p[i] << (8u * i);
    }
    return v;
}

static uint64_t get_u64le(const uint8_t *p)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < 8u; i++) {
        v |= (uint64_t)p[i] << (8u * i);
    }
    return v;
}

/* ---- frame受信 ---- */

void ts_jp_parser_init(ts_jp_parser_t *p, ts_jp_frame_cb on_frame, void *ctx)
{
    memset(p, 0, sizeof(*p));
    p->on_frame = on_frame;
    p->ctx = ctx;
}

static void record_error(ts_jp_parser_t *p, uint16_t err)
{
    switch (err) {
    case TS_JP_ERR_FRAME_CRC:
        p->stats.crc_errors++;
        break;
    case TS_JP_ERR_FRAME_LENGTH:
        p->stats.length_errors++;
        break;
    case TS_JP_ERR_FRAME_VERSION:
        p->stats.version_errors++;
        break;
    default:
        p->stats.timeouts++;
        break;
    }
    p->stats.last_error = err;
    if (p->stats.consecutive_errors < UINT8_MAX) {
        p->stats.consecutive_errors++;
    }
}

/*
 * 1 byteを組立て中のframeへ足す。frame errorならerror codeを返し、bufferは残す
 * （呼出し側が先頭byteを捨てて再走査する）。
 */
static uint16_t feed(ts_jp_parser_t *p, uint8_t b, uint32_t stamp)
{
    if (p->len == 1u && b != TS_JP_MAGIC1) {
        /* 'T'の次が'S'でなければ、その'T'は雑音 */
        p->len = 0;
        p->stats.dropped_bytes++;
    }
    if (p->len == 0u && b != TS_JP_MAGIC0) {
        p->stats.dropped_bytes++;
        return 0u;
    }
    p->buf[p->len] = b;
    p->stamp[p->len] = stamp;
    p->len++;

    if (p->len == 3u && b != TS_JP_VERSION) {
        return TS_JP_ERR_FRAME_VERSION;
    }
    if (p->len < 6u) {
        return 0u;
    }
    const uint16_t payload_length = get_u16le(&p->buf[4]);
    if (payload_length > TS_JP_MAX_PAYLOAD) {
        return TS_JP_ERR_FRAME_LENGTH;
    }
    const size_t body = TS_JP_HEADER_LEN + payload_length;
    if (p->len < body + TS_JP_CRC_LEN) {
        return 0u;
    }
    if (ts_crc32c(p->buf, body) != get_u32le(&p->buf[body])) {
        return TS_JP_ERR_FRAME_CRC;
    }

    const ts_jp_frame_t frame = {
        .command = p->buf[3],
        .sequence = get_u16le(&p->buf[6]),
        .payload_length = payload_length,
        .payload = &p->buf[TS_JP_HEADER_LEN],
        .rx_complete_ms = stamp,
    };
    p->stats.frames_ok++;
    p->stats.consecutive_errors = 0;
    if (p->on_frame != NULL) {
        p->on_frame(p->ctx, &frame);
    }
    p->len = 0;
    return 0u;
}

/*
 * wb/ws[0..n)を順に処理する。frame errorのたびに候補frameの先頭byteを捨て、
 * 残りのbyteを未処理byteの前へ戻して次のmagicを探し直す。
 * 統計へ入れるのは最初のerrorだけとし、再走査中のerrorは数えない。
 */
static void run(ts_jp_parser_t *p, uint8_t *wb, uint32_t *ws, size_t n, bool count_error)
{
    size_t i = 0;

    while (i < n) {
        const uint16_t err = feed(p, wb[i], ws[i]);
        i++;
        if (err == 0u) {
            continue;
        }
        if (count_error) {
            record_error(p, err);
            count_error = false;
        }
        const size_t keep = (size_t)p->len - 1u;
        const size_t rest = n - i;
        memmove(&wb[keep], &wb[i], rest);
        memmove(&ws[keep], &ws[i], rest * sizeof(ws[0]));
        memcpy(wb, &p->buf[1], keep);
        memcpy(ws, &p->stamp[1], keep * sizeof(ws[0]));
        n = keep + rest;
        i = 0;
        p->len = 0;
    }
}

static void expire(ts_jp_parser_t *p, uint32_t now_ms)
{
    uint8_t wb[TS_JP_MAX_FRAME + 1u];
    uint32_t ws[TS_JP_MAX_FRAME + 1u];

    if (p->len == 0u || (uint32_t)(now_ms - p->stamp[p->len - 1u]) <= TS_JP_INTERBYTE_TIMEOUT_MS) {
        return;
    }
    record_error(p, TS_JP_ERR_FRAME_TIMEOUT);

    /* 古いbyteの途中から別のframeが始まっていた場合に備えて再走査する。
       再走査後に残る未完成部分も最後のbyteが同じく古いので捨てる */
    const size_t n = (size_t)p->len - 1u;
    memcpy(wb, &p->buf[1], n);
    memcpy(ws, &p->stamp[1], n * sizeof(ws[0]));
    p->len = 0;
    run(p, wb, ws, n, false);
    p->len = 0;
}

void ts_jp_parser_push(ts_jp_parser_t *p, uint8_t byte, uint32_t now_ms)
{
    uint8_t wb[TS_JP_MAX_FRAME + 1u];
    uint32_t ws[TS_JP_MAX_FRAME + 1u];

    expire(p, now_ms);
    wb[0] = byte;
    ws[0] = now_ms;
    run(p, wb, ws, 1u, true);
}

void ts_jp_parser_poll(ts_jp_parser_t *p, uint32_t now_ms)
{
    expire(p, now_ms);
}

/* ---- frame送信 ---- */

size_t ts_jp_encode_frame(uint8_t *out, size_t cap, uint8_t command, uint16_t sequence,
                          const uint8_t *payload, uint16_t payload_length)
{
    const size_t body = TS_JP_HEADER_LEN + payload_length;

    if (payload_length > TS_JP_MAX_PAYLOAD || cap < body + TS_JP_CRC_LEN ||
        (payload_length > 0u && payload == NULL)) {
        return 0u;
    }
    out[0] = TS_JP_MAGIC0;
    out[1] = TS_JP_MAGIC1;
    out[2] = TS_JP_VERSION;
    out[3] = command;
    put_u16le(&out[4], payload_length);
    put_u16le(&out[6], sequence);
    if (payload_length > 0u) {
        memcpy(&out[TS_JP_HEADER_LEN], payload, payload_length);
    }
    put_u32le(&out[body], ts_crc32c(out, body));
    return body + TS_JP_CRC_LEN;
}

/* ---- payload ---- */

ts_jp_result_t ts_jp_decode_time_set(const uint8_t *payload, uint16_t len, ts_jp_time_set_t *out)
{
    ts_jp_time_set_t t;

    if (len != TS_JP_TIME_SET_LEN) {
        return TS_JP_NACK_BAD_LENGTH;
    }
    t.unix_sec = (int64_t)get_u64le(&payload[SET_OFF_UNIX]);
    t.nanoseconds = get_u32le(&payload[SET_OFF_NS]);
    t.valid_for_ms = get_u32le(&payload[SET_OFF_VALID]);
    t.source_id = get_u32le(&payload[SET_OFF_SOURCE]);

    if (t.nanoseconds > 999999999u || t.valid_for_ms < 1u || t.valid_for_ms > 5000u ||
        !ts_unix_in_rtc_range(t.unix_sec)) {
        return TS_JP_NACK_OUT_OF_RANGE;
    }
    *out = t;
    return TS_JP_RESULT_OK;
}

void ts_jp_encode_time_set(const ts_jp_time_set_t *in, uint8_t out[TS_JP_TIME_SET_LEN])
{
    put_u64le(&out[SET_OFF_UNIX], (uint64_t)in->unix_sec);
    put_u32le(&out[SET_OFF_NS], in->nanoseconds);
    put_u32le(&out[SET_OFF_VALID], in->valid_for_ms);
    put_u32le(&out[SET_OFF_SOURCE], in->source_id);
}

bool ts_jp_time_set_expired(uint32_t rx_complete_ms, uint32_t valid_for_ms, uint32_t now_ms)
{
    return (uint32_t)(now_ms - rx_complete_ms) > valid_for_ms;
}

void ts_jp_pack_time_status(const ts_jp_time_status_t *in, uint8_t out[TS_JP_TIME_STATUS_LEN])
{
    put_u64le(&out[ST_OFF_UNIX], (uint64_t)in->unix_sec);
    put_u32le(&out[ST_OFF_NS], in->nanoseconds);
    out[ST_OFF_STATE] = in->state;
    out[ST_OFF_FLAGS] = (uint8_t)(in->flags & TS_JP_FLAGS_DEFINED);
    put_u16le(&out[ST_OFF_RESERVED], 0u);
    put_u32le(&out[ST_OFF_SOURCE], in->last_source_id);
    put_u16le(&out[ST_OFF_SEQUENCE], in->last_sequence);
    put_u16le(&out[ST_OFF_CLOCK_DIAG], 0u);
    put_u16le(&out[ST_OFF_LAST_ERROR], in->last_error);
}

void ts_jp_unpack_time_status(const uint8_t in[TS_JP_TIME_STATUS_LEN], ts_jp_time_status_t *out)
{
    out->unix_sec = (int64_t)get_u64le(&in[ST_OFF_UNIX]);
    out->nanoseconds = get_u32le(&in[ST_OFF_NS]);
    out->state = in[ST_OFF_STATE];
    out->flags = in[ST_OFF_FLAGS];
    out->last_source_id = get_u32le(&in[ST_OFF_SOURCE]);
    out->last_sequence = get_u16le(&in[ST_OFF_SEQUENCE]);
    out->last_error = get_u16le(&in[ST_OFF_LAST_ERROR]);
}

void ts_jp_pack_ack(uint8_t request_command, uint8_t code, uint8_t out[TS_JP_ACK_LEN])
{
    out[0] = request_command;
    out[1] = code;
}

/* ---- 再送の判定 ---- */

void ts_jp_dedup_init(ts_jp_dedup_t *d)
{
    memset(d, 0, sizeof(*d));
}

ts_jp_dedup_result_t ts_jp_dedup_check(const ts_jp_dedup_t *d, const ts_jp_frame_t *f)
{
    if (!d->valid || d->sequence != f->sequence) {
        return TS_JP_DEDUP_NEW;
    }
    if (d->command == f->command && d->payload_length == f->payload_length &&
        memcmp(d->payload, f->payload, f->payload_length) == 0) {
        return TS_JP_DEDUP_REPLAY;
    }
    return TS_JP_DEDUP_CONFLICT;
}

void ts_jp_dedup_store(ts_jp_dedup_t *d, const ts_jp_frame_t *f, const uint8_t *response, size_t response_length)
{
    if (f->payload_length > TS_JP_MAX_PAYLOAD || response_length > TS_JP_MAX_FRAME) {
        d->valid = false;
        return;
    }
    d->valid = true;
    d->command = f->command;
    d->sequence = f->sequence;
    d->payload_length = f->payload_length;
    memcpy(d->payload, f->payload, f->payload_length);
    memcpy(d->response, response, response_length);
    d->response_length = (uint8_t)response_length;
}
