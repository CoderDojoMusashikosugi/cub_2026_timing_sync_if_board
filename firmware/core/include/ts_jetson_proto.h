/*
 * Jetson時刻設定protocolのframe層とpayload codec
 *
 * 仕様の正本は hardware/overall/jetson_time_protocol.md。
 * 時刻の適用（step/slew、GNSS有効中の拒否、大offsetの拒否）は状態機械側で行い、本moduleは扱わない。
 */
#ifndef TS_JETSON_PROTO_H
#define TS_JETSON_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TS_JP_MAGIC0 0x54u /* 'T' */
#define TS_JP_MAGIC1 0x53u /* 'S' */
#define TS_JP_VERSION 0x01u

#define TS_JP_HEADER_LEN 8u
#define TS_JP_CRC_LEN 4u
#define TS_JP_MAX_PAYLOAD 48u
#define TS_JP_MAX_FRAME 64u
/* 前のbyteからこの時間を超えて空いたら組立て中のframeを捨てる（20 msちょうどは有効） */
#define TS_JP_INTERBYTE_TIMEOUT_MS 20u

/* command */
#define TS_JP_CMD_GET_STATUS 0x01u
#define TS_JP_CMD_SET_TIME 0x02u
#define TS_JP_CMD_FORCE_SET_TIME 0x03u
#define TS_JP_CMD_STATUS 0x81u
#define TS_JP_CMD_ACK 0x82u
#define TS_JP_CMD_NACK 0x83u

/* payload長 */
#define TS_JP_TIME_SET_LEN 20u
#define TS_JP_TIME_STATUS_LEN 26u
#define TS_JP_ACK_LEN 2u

/*
 * ACK/NACKのresult/error code。protocolでは「実装enum」としているため本headerを正とし、
 * Jetson側実装と共有する。
 */
typedef enum {
    TS_JP_RESULT_OK = 0x00,
    TS_JP_NACK_GNSS_ACTIVE = 0x01,
    TS_JP_NACK_OFFSET_TOO_LARGE = 0x02,
    TS_JP_NACK_RTC_WRITE = 0x03,
    TS_JP_NACK_EXPIRED = 0x04,
    TS_JP_NACK_OUT_OF_RANGE = 0x05,
    TS_JP_NACK_BAD_LENGTH = 0x06,
    TS_JP_NACK_UNKNOWN_COMMAND = 0x07,
} ts_jp_result_t;

/*
 * TIME_STATUSのlast error。0=none、下位byteが0x01～0xFFなら直近に返したNACKのcode、
 * 0x01xxはframe層のerror。
 */
#define TS_JP_ERR_NONE 0x0000u
#define TS_JP_ERR_FRAME_CRC 0x0101u
#define TS_JP_ERR_FRAME_LENGTH 0x0102u
#define TS_JP_ERR_FRAME_VERSION 0x0103u
#define TS_JP_ERR_FRAME_TIMEOUT 0x0104u

/* TIME_STATUSのstate（wire値） */
typedef enum {
    TS_JP_STATE_INIT_INVALID = 0,
    TS_JP_STATE_GNSS_ACQUIRE = 1,
    TS_JP_STATE_GNSS_LOCKED = 2,
    TS_JP_STATE_JETSON_SET = 3,
    TS_JP_STATE_TCXO_HOLDOVER = 4,
    TS_JP_STATE_RESYNC = 5,
    TS_JP_STATE_FAULT = 6,
    TS_JP_STATE_FAULT_CLOCK = 7,
} ts_jp_state_t;

/* TIME_STATUSのflags */
#define TS_JP_FLAG_RTC_VALID (1u << 0)
#define TS_JP_FLAG_GNSS_VALID (1u << 1)
#define TS_JP_FLAG_CLOCK_FAULT (1u << 2)
#define TS_JP_FLAG_LSE_READY (1u << 4)
#define TS_JP_FLAGS_DEFINED (TS_JP_FLAG_RTC_VALID | TS_JP_FLAG_GNSS_VALID | TS_JP_FLAG_CLOCK_FAULT | TS_JP_FLAG_LSE_READY)

/* ---- frame受信 ---- */

typedef struct {
    uint8_t command;
    uint16_t sequence;
    uint16_t payload_length;
    const uint8_t *payload;   /* parser内部bufferを指す。callbackの中でだけ有効 */
    uint32_t rx_complete_ms;  /* 最後のbyteを受けた時刻 */
} ts_jp_frame_t;

typedef void (*ts_jp_frame_cb)(void *ctx, const ts_jp_frame_t *frame);

typedef struct {
    uint32_t frames_ok;
    uint32_t crc_errors;
    uint32_t length_errors;
    uint32_t version_errors;
    uint32_t timeouts;
    uint32_t dropped_bytes;     /* frame外の雑音byte */
    uint8_t consecutive_errors; /* 正常frameで0に戻る。255で飽和 */
    uint16_t last_error;        /* TS_JP_ERR_FRAME_* */
} ts_jp_parser_stats_t;

typedef struct {
    uint8_t buf[TS_JP_MAX_FRAME];
    uint32_t stamp[TS_JP_MAX_FRAME];
    uint8_t len;
    ts_jp_frame_cb on_frame;
    void *ctx;
    ts_jp_parser_stats_t stats;
} ts_jp_parser_t;

void ts_jp_parser_init(ts_jp_parser_t *p, ts_jp_frame_cb on_frame, void *ctx);

/*
 * 1 byteを入れる。magic/version/length/CRCが正しいframeが揃うたびにon_frameを呼ぶ。
 * frame errorでは候補frameの先頭byteだけを捨て、残りのbyteから次のmagicを探し直す。
 * on_frameの中からparserを呼んではならない。
 */
void ts_jp_parser_push(ts_jp_parser_t *p, uint8_t byte, uint32_t now_ms);

/* byteが来ない間のinter-byte timeoutを判定する。main loopから定期的に呼ぶ */
void ts_jp_parser_poll(ts_jp_parser_t *p, uint32_t now_ms);

/* ---- frame送信 ---- */

/* frameを組み立てて長さを返す。payload_length > 48やcap不足なら0 */
size_t ts_jp_encode_frame(uint8_t *out, size_t cap, uint8_t command, uint16_t sequence,
                          const uint8_t *payload, uint16_t payload_length);

/* ---- payload ---- */

typedef struct {
    int64_t unix_sec;
    uint32_t nanoseconds;
    uint32_t valid_for_ms;
    uint32_t source_id;
} ts_jp_time_set_t;

/*
 * TIME_SETを解析し範囲を検証する。Unix秒はRTCが表せる2000～2099年に限る
 * （さらに狭めるかはfirmware/test_plan.md のP-17で決める）。
 */
ts_jp_result_t ts_jp_decode_time_set(const uint8_t *payload, uint16_t len, ts_jp_time_set_t *out);
void ts_jp_encode_time_set(const ts_jp_time_set_t *in, uint8_t out[TS_JP_TIME_SET_LEN]);

/* 受信完了からvalid_for_msを超えて経過していればtrue（tick wrap対応） */
bool ts_jp_time_set_expired(uint32_t rx_complete_ms, uint32_t valid_for_ms, uint32_t now_ms);

typedef struct {
    int64_t unix_sec;
    uint32_t nanoseconds;
    uint8_t state;           /* ts_jp_state_t */
    uint8_t flags;           /* TS_JP_FLAG_*。未定義bitは0にして送る */
    uint32_t last_source_id; /* 未適用なら0 */
    uint16_t last_sequence;  /* 未適用なら0 */
    uint16_t last_error;
} ts_jp_time_status_t;

/* clock diagnosticはDR-050で0固定のため常に0を書く */
void ts_jp_pack_time_status(const ts_jp_time_status_t *in, uint8_t out[TS_JP_TIME_STATUS_LEN]);
void ts_jp_unpack_time_status(const uint8_t in[TS_JP_TIME_STATUS_LEN], ts_jp_time_status_t *out);

void ts_jp_pack_ack(uint8_t request_command, uint8_t code, uint8_t out[TS_JP_ACK_LEN]);

/* ---- 再送の判定 ---- */

/*
 * SET_TIME/FORCE_SET_TIMEの再送を検出するため、最後に処理したrequestと応答frameを覚える。
 * GET_STATUSには使わない（状態が変わるため毎回作り直す）。
 */
typedef struct {
    bool valid;
    uint8_t command;
    uint16_t sequence;
    uint16_t payload_length;
    uint8_t payload[TS_JP_MAX_PAYLOAD];
    uint8_t response[TS_JP_MAX_FRAME];
    uint8_t response_length;
} ts_jp_dedup_t;

typedef enum {
    TS_JP_DEDUP_NEW = 0,  /* 新しいrequest */
    TS_JP_DEDUP_REPLAY,   /* 同じsequence・同じcommand・同じpayload。保存した応答をそのまま返す */
    TS_JP_DEDUP_CONFLICT, /* 同じsequenceで内容が違う。扱いはP-12で決める */
} ts_jp_dedup_result_t;

void ts_jp_dedup_init(ts_jp_dedup_t *d);
ts_jp_dedup_result_t ts_jp_dedup_check(const ts_jp_dedup_t *d, const ts_jp_frame_t *f);
void ts_jp_dedup_store(ts_jp_dedup_t *d, const ts_jp_frame_t *f, const uint8_t *response, size_t response_length);

#endif /* TS_JETSON_PROTO_H */
