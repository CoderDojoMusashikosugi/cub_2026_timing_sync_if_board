/*
 * オシロや外部機器なしで基板の状態を確かめる診断command
 *
 *   gpio    全60 I/O pinのmode/pull/AF/出力値をbring-up firmwareの想定と照合する（test_plan IO-02、IO-06）
 *   inputs  U4（SN74LV14A）出力の静的レベル。コネクタ未接続時のbias抵抗と反転を確かめる（UA-01の一部）
 *   rxidle  受信UART 4系統をRXINV=1で有効にし、未接続のまま誤受信・framing errorが出ないことを見る
 */
#ifndef DIAG_H
#define DIAG_H

void diag_gpio(const char *arg);
void diag_inputs(const char *arg);
void diag_rxidle(const char *arg);

#endif /* DIAG_H */
