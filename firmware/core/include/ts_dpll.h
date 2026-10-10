/*
 * 生成PPS（TIM2）をGNSS PPSへ規律するDPLLの制御則
 *
 * 生成PPSの秒境界B_kはTIM2のupdate、GNSS PPSは同じTIM2のCH1 captureで観測する。周期の長さN（ARR+1）だけを
 * 変えて位相と周波数を合わせる（pin_assignment「PPS/holdoverのtimer構成」）。ARRはpreload（ARPE=1）なので、
 * 境界B_{k+1}で決めた値は期間k+2の長さになる。この1周期の遅れは予測で補う。
 *
 * 誤差E = GNSS edgeのcapture位置 − 対応する秒境界 − setpoint（count）。正ならedgeが境界より後、つまり
 * 生成PPSが早いので周期を延ばす。setpointは出力buffer＋入力経路の遅れで、合っているときのcapture値。
 */
#ifndef TS_DPLL_H
#define TS_DPLL_H

#include <stdbool.h>
#include <stdint.h>

#define TS_DPLL_NOMINAL 48000000
/* 出力中の周期の上限・下限: |N − NOMINAL| ≤ 4800（PPS間隔0.9999～1.0001秒、補正±100 us/s。design §4.1） */
#define TS_DPLL_SLEW_LIMIT 4800
/* align（出力High-Z中だけ使う）で許す周期の範囲 */
#define TS_DPLL_MIN_PERIOD 24000000
#define TS_DPLL_MAX_PERIOD 72000000
/* 周波数推定Fの範囲（±100 ppm） */
#define TS_DPLL_FREQ_LIMIT 4800
/* |予測誤差|がこれ以上のときは周波数推定を更新しない（RESYNCの大きな位相差で積分が暴れないように） */
#define TS_DPLL_INTEGRATE_LIMIT 2400

typedef enum {
    TS_DPLL_HOLD = 0, /* N = NOMINAL + F（＋slew）。GNSS captureは使わない */
    TS_DPLL_TRACK,    /* GNSS captureで位相と周波数を追う */
} ts_dpll_mode_t;

typedef struct {
    ts_dpll_mode_t mode;
    int32_t setpoint;  /* 合っているときのcapture値（count） */
    int32_t gate;      /* TRACKで|E|がこれを超えるcaptureは使わない（glitch除外）。0なら制限なし */
    int64_t freq_q16;  /* F: TCXOの周波数偏差（count/秒、Q16）。holdoverはこの値を保持する */
    int64_t frac_q16;  /* 周期の小数部の繰越し（Q16） */
    int64_t slew;      /* HOLD中に周期へ分けて加える残りの位相（count、正で周期を延ばす） */

    /* align: 次の境界処理で1回だけ、ある事象の位置へ秒境界を揃える（出力High-Z中だけ使う） */
    bool align_pending;
    uint64_t align_pos; /* 揃えたい事象の絶対位置（count）。事象は以後NOMINAL+Fごとに繰り返すとみなす */
    bool align_done;
    uint32_t align_index; /* 揃えた秒境界の番号 */
    int32_t align_cycles; /* その境界はalign_posから何秒後の事象に当たるか */

    /* 診断（最後の境界処理） */
    bool last_used;    /* captureを制御に使った */
    int32_t last_err;  /* B_{k+1}に対する誤差（予測を含む） */
    int32_t last_pred; /* B_{k+2}に対する予測誤差 */
} ts_dpll_t;

typedef struct {
    uint64_t base_k1;  /* B_{k+1}の絶対位置（count） */
    uint32_t index_k1; /* B_{k+1}の番号 */
    bool have_capture; /* 期間kにGNSS captureがちょうど1回あった */
    uint32_t capture;  /* そのCCR1（B_kからのcount） */
    uint32_t n_k;      /* 期間kの長さ */
    uint32_t n_k1;     /* 期間k+1の長さ（B_{k+1}でloadされた値） */
} ts_dpll_in_t;

void ts_dpll_init(ts_dpll_t *d, int32_t setpoint);

/*
 * 期間kのcapture位置を、最も近い秒境界に対する誤差へ直す。
 * *pairs_nextがtrueならedgeはB_{k+1}に対応する（境界より前に来た）。falseならB_kに対応する。
 */
int32_t ts_dpll_edge_error(uint32_t capture, uint32_t n_k, int32_t setpoint, bool *pairs_next);

/* 秒境界B_{k+1}で呼び、期間k+2の長さNを返す（ARRには N−1 を書く） */
uint32_t ts_dpll_boundary(ts_dpll_t *d, const ts_dpll_in_t *in);

/* 周波数推定をcount/秒（Q16）で設定する。範囲外は丸める */
void ts_dpll_set_freq(ts_dpll_t *d, int64_t freq_q16);

#endif /* TS_DPLL_H */
