/*
 * test buildだけに入る試験用command（firmware/test_plan.md §3.3）
 *
 *   ut         hostのL1試験（tests/host/test_*.c）をMCU上で実行する（UT-13）
 *   fault css  次の起動をHSE CSSの後と同じclock診断mode（HSI16、USB=HSI48+CRS）にする（CK-05、P-20）
 *   loop       J3/J7/J8のpin 3―pin 4短絡、またはJ3 pin 3→J2 pin 3でUART経路とOE_Nを確かめる（UA-03、IO-03）
 *   ppsloop    J3 pin 2→J2 pin 2で生成PPSをGNSS PPS captureへ折り返す（PP-03/PP-04の一部）。どちらもselftest_loop.c
 */
#ifndef SELFTEST_H
#define SELFTEST_H

void selftest_ut(const char *arg);
void selftest_fault(const char *arg);
void selftest_loop(const char *arg);
void selftest_ppsloop(const char *arg);

#endif /* SELFTEST_H */
