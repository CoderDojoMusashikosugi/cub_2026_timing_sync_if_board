/*
 * test buildだけに入る試験用command（firmware/test_plan.md §3.3）
 *
 *   ut         hostのL1試験（tests/host/test_*.c）をMCU上で実行する（UT-13）
 *   fault css  次の起動をHSE CSSの後と同じclock診断mode（HSI16、USB=HSI48+CRS）にする（CK-05、P-20）
 */
#ifndef SELFTEST_H
#define SELFTEST_H

void selftest_ut(const char *arg);
void selftest_fault(const char *arg);

#endif /* SELFTEST_H */
