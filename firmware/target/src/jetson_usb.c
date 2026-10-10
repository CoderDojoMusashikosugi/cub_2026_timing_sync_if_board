/*
 * Jetson時刻protocol（hardware/overall/jetson_time_protocol.md）をUSB CDCの2つ目のinterfaceで受ける
 *
 * Jetsonとの双方向通信はUSB経由にした（J3のUARTはNMEA送信だけ）。frame・CRC・timeoutの扱いはUARTのときと同じ。
 * consoleはinterface 0、本protocolはinterface 2（Linuxでは…-if02のttyACM）。
 */
#include "jetson_usb.h"

#include <string.h>

#include "board.h"
#include "timesync.h"
#include "ts_jetson_proto.h"
#include "tusb.h"

#define CDC_ITF 1u

static ts_jp_parser_t parser;
static ts_jp_dedup_t dedup;
static uint32_t last_source_id;
static uint16_t last_sequence;
static uint16_t last_error;

/* ACKを保留しているSET（時刻の適用とRTC書込みの完了待ち） */
static bool pending;
static ts_jp_frame_t pending_frame;
static uint8_t pending_payload[TS_JP_MAX_PAYLOAD];
static uint32_t pending_source_id;

static size_t send(uint8_t command, uint16_t sequence, const uint8_t *payload, uint16_t len, uint8_t *frame)
{
    const size_t n = ts_jp_encode_frame(frame, TS_JP_MAX_FRAME, command, sequence, payload, len);
    if (n > 0u && tud_cdc_n_connected(CDC_ITF)) {
        tud_cdc_n_write(CDC_ITF, frame, (uint32_t)n);
        tud_cdc_n_write_flush(CDC_ITF);
    }
    return n;
}

static void send_ack(const ts_jp_frame_t *f, uint8_t code, bool store)
{
    uint8_t payload[TS_JP_ACK_LEN];
    uint8_t frame[TS_JP_MAX_FRAME];

    ts_jp_pack_ack(f->command, code, payload);
    const size_t n = send(code == TS_JP_RESULT_OK ? TS_JP_CMD_ACK : TS_JP_CMD_NACK, f->sequence, payload,
                          TS_JP_ACK_LEN, frame);
    if (code != TS_JP_RESULT_OK) {
        last_error = code;
    }
    if (store && n > 0u) {
        ts_jp_dedup_store(&dedup, f, frame, n);
    }
}

static void send_status(const ts_jp_frame_t *f)
{
    timesync_status_t st;
    ts_jp_time_status_t s;
    uint8_t payload[TS_JP_TIME_STATUS_LEN];
    uint8_t frame[TS_JP_MAX_FRAME];
    board_lse_status_t lse;

    timesync_get_status(&st);
    board_lse_status(&lse);
    memset(&s, 0, sizeof(s));
    s.unix_sec = st.time_valid ? st.unix_sec : 0;
    s.nanoseconds = st.time_valid ? st.nanoseconds : 0u;
    s.state = st.state;
    s.flags = (uint8_t)((st.rtc_valid ? TS_JP_FLAG_RTC_VALID : 0u) | (st.gnss_valid ? TS_JP_FLAG_GNSS_VALID : 0u) |
                        (st.state == TS_JP_STATE_FAULT_CLOCK ? TS_JP_FLAG_CLOCK_FAULT : 0u) |
                        (lse.ready ? TS_JP_FLAG_LSE_READY : 0u));
    s.last_source_id = last_source_id;
    s.last_sequence = last_sequence;
    s.last_error = last_error != 0u ? last_error : parser.stats.last_error;
    ts_jp_pack_time_status(&s, payload);
    (void)send(TS_JP_CMD_STATUS, f->sequence, payload, TS_JP_TIME_STATUS_LEN, frame);
}

static void on_frame(void *ctx, const ts_jp_frame_t *f)
{
    (void)ctx;
    switch (f->command) {
    case TS_JP_CMD_GET_STATUS:
        if (f->payload_length != 0u) {
            send_ack(f, TS_JP_NACK_BAD_LENGTH, false);
        } else {
            send_status(f);
        }
        break;
    case TS_JP_CMD_SET_TIME:
    case TS_JP_CMD_FORCE_SET_TIME: {
        if (pending && f->command == pending_frame.command && f->sequence == pending_frame.sequence &&
            f->payload_length == pending_frame.payload_length &&
            memcmp(f->payload, pending_payload, f->payload_length) == 0) {
            break; /* 保留中のSETの再送: 適用し終えたら1回だけ応答する */
        }
        if (ts_jp_dedup_check(&dedup, f) == TS_JP_DEDUP_REPLAY) {
            /* 同じsequence・同じpayloadの再送: 保存した応答をそのまま返し、二重に適用しない */
            if (tud_cdc_n_connected(CDC_ITF)) {
                tud_cdc_n_write(CDC_ITF, dedup.response, dedup.response_length);
                tud_cdc_n_write_flush(CDC_ITF);
            }
            break;
        }
        /* 同じsequenceで内容が違う場合（P-12未決定）は新しいrequestとして扱う */
        ts_jp_time_set_t ts;
        ts_jp_result_t r = ts_jp_decode_time_set(f->payload, f->payload_length, &ts);
        if (r == TS_JP_RESULT_OK && ts_jp_time_set_expired(f->rx_complete_ms, ts.valid_for_ms, board_millis())) {
            r = TS_JP_NACK_EXPIRED;
        }
        if (r == TS_JP_RESULT_OK) {
            r = timesync_jetson_set(&ts, f->rx_complete_ms, f->command == TS_JP_CMD_FORCE_SET_TIME);
        }
        if (r == TS_JP_RESULT_OK && timesync_jetson_waiting()) {
            /* stepは秒境界を2つ以上待つので、適用・RTC書込み・backup commitが済むまでACKを保留する */
            pending = true;
            pending_frame = *f;
            memcpy(pending_payload, f->payload, f->payload_length);
            pending_frame.payload = pending_payload;
            pending_source_id = ts.source_id;
            break;
        }
        if (r == TS_JP_RESULT_OK) {
            last_source_id = ts.source_id;
            last_sequence = f->sequence;
        }
        send_ack(f, (uint8_t)r, true);
        break;
    }
    default:
        send_ack(f, TS_JP_NACK_UNKNOWN_COMMAND, false);
        break;
    }
}

void jetson_usb_init(void)
{
    ts_jp_parser_init(&parser, on_frame, NULL);
    ts_jp_dedup_init(&dedup);
}

void jetson_usb_task(void)
{
    uint8_t buf[64];

    while (tud_cdc_n_available(CDC_ITF) > 0u) {
        const uint32_t n = tud_cdc_n_read(CDC_ITF, buf, sizeof(buf));
        const uint32_t now = board_millis();
        for (uint32_t i = 0; i < n; i++) {
            ts_jp_parser_push(&parser, buf[i], now);
        }
        if (n == 0u) {
            break;
        }
    }
    ts_jp_parser_poll(&parser, board_millis());

    uint8_t code;
    if (timesync_jetson_result(&code) && pending) {
        pending = false;
        if (code == TS_JP_RESULT_OK) {
            last_source_id = pending_source_id;
            last_sequence = pending_frame.sequence;
        }
        send_ack(&pending_frame, code, true);
    }
}
