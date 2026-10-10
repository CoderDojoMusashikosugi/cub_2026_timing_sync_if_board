#include "ts_dpll.h"

#include <string.h>

#define Q16 INT64_C(65536)
#define NOM ((int64_t)TS_DPLL_NOMINAL)
/* 比例gain 1/4、積分gain 1/64（閉ループの数値simulationで整定約30秒、overshoot約10 %） */
#define KP_DIV 4
#define KI_DIV 64

static int64_t div_round(int64_t a, int64_t b)
{
    return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b);
}

static int64_t div_floor(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b) != 0 && a < 0) {
        q--;
    }
    return q;
}

static int64_t clamp64(int64_t v, int64_t lo, int64_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void ts_dpll_init(ts_dpll_t *d, int32_t setpoint)
{
    memset(d, 0, sizeof(*d));
    d->mode = TS_DPLL_HOLD;
    d->setpoint = setpoint;
}

int32_t ts_dpll_edge_error(uint32_t capture, uint32_t n_k, int32_t setpoint, bool *pairs_next)
{
    int64_t x = (int64_t)capture - setpoint;
    const bool next = x > (int64_t)(n_k / 2u);

    if (next) {
        x -= (int64_t)n_k;
    }
    if (pairs_next != NULL) {
        *pairs_next = next;
    }
    return (int32_t)x;
}

void ts_dpll_set_freq(ts_dpll_t *d, int64_t freq_q16)
{
    d->freq_q16 = clamp64(freq_q16, -TS_DPLL_FREQ_LIMIT * Q16, TS_DPLL_FREQ_LIMIT * Q16);
}

/* Q16の周期を範囲に収め、小数部を次へ繰り越して整数の周期にする（平均がQ16の値に一致する） */
static uint32_t take_period(ts_dpll_t *d, int64_t n_q16, int64_t lo, int64_t hi)
{
    const int64_t total = clamp64(n_q16, lo * Q16, hi * Q16) + d->frac_q16;
    const int64_t n = div_floor(total, Q16);

    d->frac_q16 = total - n * Q16;
    return (uint32_t)clamp64(n, lo, hi);
}

static uint32_t do_align(ts_dpll_t *d, const ts_dpll_in_t *in, int64_t m_q16)
{
    const uint64_t b_k2 = in->base_k1 + in->n_k1;
    /* 絶対位置は長時間で大きくなるので、B_{k+2}からの相対値で計算する */
    const int64_t rel = (int64_t)(d->align_pos - b_k2); /* 事象の位置 − B_{k+2}（負なら過去） */
    int64_t j = div_round((NOM - rel) * Q16, m_q16);
    int64_t n_q16 = rel * Q16 + j * m_q16;

    for (int i = 0; i < 3; i++) {
        if (n_q16 < TS_DPLL_MIN_PERIOD * Q16) {
            j++;
        } else if (n_q16 > TS_DPLL_MAX_PERIOD * Q16) {
            j--;
        } else {
            break;
        }
        n_q16 = rel * Q16 + j * m_q16;
    }
    d->align_pending = false;
    d->align_done = true;
    d->align_index = in->index_k1 + 2u;
    d->align_cycles = (int32_t)j;
    d->slew = 0;
    d->frac_q16 = 0;
    return (uint32_t)clamp64(div_round(n_q16, Q16), TS_DPLL_MIN_PERIOD, TS_DPLL_MAX_PERIOD);
}

uint32_t ts_dpll_boundary(ts_dpll_t *d, const ts_dpll_in_t *in)
{
    const int64_t m_q16 = NOM * Q16 + d->freq_q16; /* GNSSの1秒に相当するcount数の推定 */

    d->last_used = false;
    if (d->align_pending) {
        return do_align(d, in, m_q16);
    }

    if (d->mode == TS_DPLL_TRACK && in->have_capture) {
        bool next;
        const int32_t x = ts_dpll_edge_error(in->capture, in->n_k, d->setpoint, &next);
        /* B_{k+1}に対応するedgeの誤差。B_kに対応したedgeなら1秒進めた予測にする */
        int64_t p1_q16 = (int64_t)x * Q16;
        if (!next) {
            p1_q16 += m_q16 - (int64_t)in->n_k * Q16;
        }
        const int64_t p1 = div_round(p1_q16, Q16);
        if (d->gate == 0 || (p1 <= d->gate && p1 >= -(int64_t)d->gate)) {
            /* 期間k+1の長さは決まっているので、B_{k+2}での誤差を予測して期間k+2で直す */
            const int64_t p2_q16 = p1_q16 + m_q16 - (int64_t)in->n_k1 * Q16;
            const int64_t p2 = div_round(p2_q16, Q16);
            if (p2 < TS_DPLL_INTEGRATE_LIMIT && p2 > -TS_DPLL_INTEGRATE_LIMIT) {
                ts_dpll_set_freq(d, d->freq_q16 + p2_q16 / KI_DIV);
            }
            d->last_used = true;
            d->last_err = (int32_t)p1;
            d->last_pred = (int32_t)clamp64(p2, INT32_MIN, INT32_MAX);
            const int64_t n_q16 = NOM * Q16 + d->freq_q16 + p2_q16 / KP_DIV;
            return take_period(d, n_q16, NOM - TS_DPLL_SLEW_LIMIT, NOM + TS_DPLL_SLEW_LIMIT);
        }
    }

    /* HOLD、またはcaptureがない・使えない: 周波数推定を保ち、slewの残りを制限内で加える */
    const int64_t f = div_round(d->freq_q16, Q16);
    const int64_t room = TS_DPLL_SLEW_LIMIT - (f < 0 ? -f : f);
    const int64_t s = room > 0 ? clamp64(d->slew, -room, room) : 0;
    d->slew -= s;
    return take_period(d, NOM * Q16 + d->freq_q16 + s * Q16, NOM - TS_DPLL_SLEW_LIMIT, NOM + TS_DPLL_SLEW_LIMIT);
}
