/* DPLL制御則（UT-10の一部）: 閉ループで回し、収束、周期制限、align、holdover、glitch除外を見る */
#include "test_harness.h"
#include "ts_dpll.h"

#define NOM ((int64_t)TS_DPLL_NOMINAL)
#define SP 6 /* 出力→入力経路の遅れ（J3→J2折返しの実測） */

/* 1本のGNSS PPS（真のedge位置）と生成PPSの境界を進める簡単なmodel */
typedef struct {
    int64_t b_k;    /* 期間kの開始位置 */
    uint32_t n_k;   /* 期間kの長さ */
    uint32_t n_k1;  /* 期間k+1の長さ */
    uint32_t index; /* k */
    int64_t edge;   /* 次のGNSS edgeの真の位置 */
    int64_t m;      /* GNSSの1秒（count） */
    uint32_t lcg;
    int32_t jitter; /* captureに加える±jitter（count） */
    bool drop;      /* このstepのcaptureを落とす */
    int64_t glitch; /* 0以外なら、このstepのcaptureをこの位置（期間内）に置き換える */
    int64_t min_n, max_n;
} sim_t;

static void sim_init(sim_t *s, int64_t first_edge, int64_t m)
{
    memset(s, 0, sizeof(*s));
    s->n_k = (uint32_t)NOM;
    s->n_k1 = (uint32_t)NOM;
    s->edge = first_edge;
    s->m = m;
    s->lcg = 12345u;
    s->min_n = INT64_MAX;
    s->max_n = INT64_MIN;
}

static int32_t sim_noise(sim_t *s)
{
    if (s->jitter == 0) {
        return 0;
    }
    s->lcg = s->lcg * 1103515245u + 12345u;
    return (int32_t)((s->lcg >> 16) % (uint32_t)(2 * s->jitter + 1)) - s->jitter;
}

/* 期間kを処理してB_{k+1}へ進める。境界B_{k+1}と最寄りedgeの差（edge − 境界）を返す */
static int64_t sim_step(sim_t *s, ts_dpll_t *d)
{
    ts_dpll_in_t in = {0};

    while (s->edge + SP < s->b_k) {
        s->edge += s->m;
    }
    const int64_t cap = s->edge + SP + sim_noise(s);
    if (cap >= s->b_k && cap < s->b_k + s->n_k) {
        in.have_capture = !s->drop;
        in.capture = (uint32_t)(cap - s->b_k);
        s->edge += s->m;
    }
    if (s->glitch != 0) {
        in.have_capture = true;
        in.capture = (uint32_t)s->glitch;
    }
    in.base_k1 = (uint64_t)(s->b_k + s->n_k);
    in.index_k1 = s->index + 1u;
    in.n_k = s->n_k;
    in.n_k1 = s->n_k1;
    const uint32_t n2 = ts_dpll_boundary(d, &in);
    if (n2 < s->min_n) {
        s->min_n = n2;
    }
    if (n2 > s->max_n) {
        s->max_n = n2;
    }
    s->b_k += s->n_k;
    s->n_k = s->n_k1;
    s->n_k1 = n2;
    s->index++;

    /* B_{k+1}（今のb_k）に最も近いedge */
    int64_t e = s->edge;
    while (e - s->m > s->b_k - s->m / 2) {
        e -= s->m;
    }
    while (e < s->b_k - s->m / 2) {
        e += s->m;
    }
    return e - s->b_k;
}

static void test_edge_error(void)
{
    bool next;
    CHECK_EQ_INT(ts_dpll_edge_error(6u, 48000000u, SP, &next), 0);
    CHECK(!next);
    CHECK_EQ_INT(ts_dpll_edge_error(1006u, 48000000u, SP, &next), 1000);
    CHECK(!next);
    CHECK_EQ_INT(ts_dpll_edge_error(47999000u, 48000000u, SP, &next), -1006);
    CHECK(next);
    CHECK_EQ_INT(ts_dpll_edge_error(0u, 48000000u, SP, &next), -6);
    CHECK(!next);
}

/* TRACKだけで3000 count（62.5 us）と+3 ppmの偏差から収束し、出力中の周期制限を守る */
static void test_track_converges(void)
{
    ts_dpll_t d;
    sim_t s;

    ts_dpll_init(&d, SP);
    d.mode = TS_DPLL_TRACK;
    sim_init(&s, 3000, NOM + 144);
    s.jitter = 1;
    int64_t worst = 0;
    for (int i = 0; i < 300; i++) {
        const int64_t e = sim_step(&s, &d);
        if (i >= 150 && (e > worst || -e > worst)) {
            worst = e < 0 ? -e : e;
        }
    }
    CHECK(worst <= 3);
    CHECK(s.min_n >= NOM - TS_DPLL_SLEW_LIMIT);
    CHECK(s.max_n <= NOM + TS_DPLL_SLEW_LIMIT);
    /* 周波数推定が真値（+144 count/秒）に合う */
    const int64_t f = d.freq_q16 / 65536;
    CHECK(f >= 143 && f <= 145);
}

/* alignで任意の位相（0.37秒ずれ）から一度に揃え、揃えた境界の番号と事象の秒数を返す */
static void test_align(void)
{
    ts_dpll_t d;
    sim_t s;

    ts_dpll_init(&d, SP);
    sim_init(&s, 17760000, NOM);
    (void)sim_step(&s, &d); /* 期間0: edgeを観測 */
    /* 期間0で見たedgeの位置（capture − setpoint）へ揃える */
    d.align_pos = (uint64_t)17760000;
    d.align_pending = true;
    (void)sim_step(&s, &d); /* B_2: alignを処理して期間3の長さを決める（揃うのはB_4） */
    CHECK(d.align_done);
    CHECK_EQ_INT(d.align_index, 4);
    CHECK(s.max_n <= TS_DPLL_MAX_PERIOD && s.min_n >= TS_DPLL_MIN_PERIOD);
    (void)sim_step(&s, &d);
    const int64_t e = sim_step(&s, &d); /* B_4 */
    CHECK(e >= -1 && e <= 1);
    /* B_4 = edge0 + align_cycles秒 */
    CHECK_EQ_INT((int64_t)17760000 + (int64_t)d.align_cycles * NOM, s.b_k);
}

/* holdoverでは周波数推定を保つ: 5分で誤差が周波数推定の誤差分しか増えない */
static void test_holdover_keeps_frequency(void)
{
    ts_dpll_t d;
    sim_t s;

    ts_dpll_init(&d, SP);
    d.mode = TS_DPLL_TRACK;
    sim_init(&s, 0, NOM + 96); /* +2 ppm */
    for (int i = 0; i < 200; i++) {
        (void)sim_step(&s, &d);
    }
    d.mode = TS_DPLL_HOLD;
    int64_t e = 0;
    for (int i = 0; i < 300; i++) {
        e = sim_step(&s, &d);
    }
    /* 1 ms = 48000 count。周波数推定の誤差が0.1 count/秒でも300秒で30 count */
    CHECK(e > -100 && e < 100);
    CHECK(s.min_n >= NOM - TS_DPLL_SLEW_LIMIT && s.max_n <= NOM + TS_DPLL_SLEW_LIMIT);
}

/* gateの外のcapture（glitch）とcaptureの欠落では周期を変えない */
static void test_gate_and_missing(void)
{
    ts_dpll_t d;
    sim_t s;

    ts_dpll_init(&d, SP);
    d.mode = TS_DPLL_TRACK;
    d.gate = 4800;
    sim_init(&s, 0, NOM);
    for (int i = 0; i < 50; i++) {
        (void)sim_step(&s, &d);
    }
    s.glitch = 24000000; /* 期間の真ん中に余分なedge */
    (void)sim_step(&s, &d);
    s.glitch = 0;
    CHECK(!d.last_used);
    s.drop = true;
    (void)sim_step(&s, &d);
    (void)sim_step(&s, &d);
    s.drop = false;
    int64_t e = 0;
    for (int i = 0; i < 3; i++) {
        e = sim_step(&s, &d);
    }
    CHECK(e >= -2 && e <= 2);
}

/* RESYNC相当: 50 msの位相差をgateなしで追うと、周期制限（±100 us/s）を守って約500秒で戻る */
static void test_slew_limited_resync(void)
{
    ts_dpll_t d;
    sim_t s;

    ts_dpll_init(&d, SP);
    d.mode = TS_DPLL_TRACK;
    sim_init(&s, 2400000, NOM + 48);
    ts_dpll_set_freq(&d, 48 * 65536);
    int64_t e = 0;
    int converged_at = -1;
    for (int i = 0; i < 700; i++) {
        e = sim_step(&s, &d);
        if (converged_at < 0 && e >= -48 && e <= 48) {
            converged_at = i;
        }
    }
    CHECK(s.min_n >= NOM - TS_DPLL_SLEW_LIMIT && s.max_n <= NOM + TS_DPLL_SLEW_LIMIT);
    CHECK(converged_at >= 480 && converged_at <= 560);
    CHECK(e >= -3 && e <= 3);
    const int64_t f = d.freq_q16 / 65536;
    CHECK(f >= 46 && f <= 50);
}

/* HOLD中のslew: 残りを±(100 us/s − |F|)ずつ周期へ分けて入れる */
static void test_hold_slew(void)
{
    ts_dpll_t d;
    ts_dpll_in_t in = {.n_k = (uint32_t)NOM, .n_k1 = (uint32_t)NOM};

    ts_dpll_init(&d, SP);
    ts_dpll_set_freq(&d, 100 * 65536);
    d.slew = -10000;
    int64_t sum = 0;
    for (int i = 0; i < 5; i++) {
        const uint32_t n = ts_dpll_boundary(&d, &in);
        CHECK(n >= NOM - TS_DPLL_SLEW_LIMIT && n <= NOM + TS_DPLL_SLEW_LIMIT);
        sum += (int64_t)n - NOM - 100;
    }
    CHECK_EQ_INT(sum, -10000);
    CHECK_EQ_INT(d.slew, 0);
}

int main(void)
{
    RUN_TEST(test_edge_error);
    RUN_TEST(test_track_converges);
    RUN_TEST(test_align);
    RUN_TEST(test_holdover_keeps_frequency);
    RUN_TEST(test_gate_and_missing);
    RUN_TEST(test_slew_limited_resync);
    RUN_TEST(test_hold_slew);
    TEST_EXIT();
}
