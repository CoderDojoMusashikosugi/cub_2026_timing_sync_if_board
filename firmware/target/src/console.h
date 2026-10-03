/*
 * USB CDCの1行commandによるconsole（bring-up用）
 *
 * command体系の正式な仕様はfirmware/test_plan.md のP-15で決める。現在はbring-upで使う
 * 診断commandだけを持つ。
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stddef.h>
#include <stdint.h>

/* main loopから呼ぶ。受信した文字を1行にまとめ、改行でcommandを実行する */
void console_task(void);

/* printf形式で送る。hostが読まない・未接続のときは待たずに捨てる */
void con_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* そのまま送る（stdoutの出力先にも使う） */
void console_write(const char *s, size_t n);

/* 送信を出し切るまでUSBを処理しながら待つ（reset前などに使う） */
void console_drain(uint32_t ms);

#endif /* CONSOLE_H */
