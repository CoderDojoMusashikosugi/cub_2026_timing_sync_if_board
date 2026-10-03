/*
 * host単体試験（L1）用の最小限のassert集
 *
 * 1つのtest fileが1つの実行ファイルになる。失敗があれば終了codeを1にする。
 * TODO()は未決定の仕様（firmware/test_plan.md のP項目）を出力に残すだけで、失敗にはしない。
 */
#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int th_failures;
static int th_checks;
static int th_todos;

#define CHECK(cond)                                                                \
    do {                                                                           \
        th_checks++;                                                               \
        if (!(cond)) {                                                             \
            th_failures++;                                                         \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                          \
    } while (0)

#define CHECK_EQ_INT(actual, expected)                                                   \
    do {                                                                                 \
        const long long th_a = (long long)(actual);                                      \
        const long long th_e = (long long)(expected);                                    \
        th_checks++;                                                                     \
        if (th_a != th_e) {                                                              \
            th_failures++;                                                               \
            fprintf(stderr, "%s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__,    \
                    #actual, th_a, th_e);                                                \
        }                                                                                \
    } while (0)

#define CHECK_EQ_HEX(actual, expected)                                                         \
    do {                                                                                       \
        const unsigned long long th_a = (unsigned long long)(actual);                          \
        const unsigned long long th_e = (unsigned long long)(expected);                        \
        th_checks++;                                                                           \
        if (th_a != th_e) {                                                                    \
            th_failures++;                                                                     \
            fprintf(stderr, "%s:%d: %s == 0x%llX, expected 0x%llX\n", __FILE__, __LINE__,      \
                    #actual, th_a, th_e);                                                      \
        }                                                                                      \
    } while (0)

#define CHECK_EQ_STR(actual, expected)                                                        \
    do {                                                                                      \
        const char *th_a = (actual);                                                          \
        const char *th_e = (expected);                                                        \
        th_checks++;                                                                          \
        if (strcmp(th_a, th_e) != 0) {                                                        \
            th_failures++;                                                                    \
            fprintf(stderr, "%s:%d: %s == \"%s\", expected \"%s\"\n", __FILE__, __LINE__,     \
                    #actual, th_a, th_e);                                                     \
        }                                                                                     \
    } while (0)

#define CHECK_EQ_MEM(actual, expected, n)                                                \
    do {                                                                                 \
        th_checks++;                                                                     \
        if (memcmp((actual), (expected), (n)) != 0) {                                    \
            th_failures++;                                                               \
            fprintf(stderr, "%s:%d: %s differs from %s\n", __FILE__, __LINE__, #actual,  \
                    #expected);                                                          \
        }                                                                                \
    } while (0)

#define TODO(msg)                                    \
    do {                                             \
        th_todos++;                                  \
        printf("TODO %s:%d: %s\n", __FILE__, __LINE__, msg); \
    } while (0)

#define RUN_TEST(fn)                                                        \
    do {                                                                    \
        const int th_before = th_failures;                                  \
        fn();                                                               \
        printf("%s %s\n", th_failures == th_before ? "PASS" : "FAIL", #fn); \
    } while (0)

#define TEST_EXIT()                                                                      \
    do {                                                                                 \
        printf("checks=%d failures=%d todos=%d\n", th_checks, th_failures, th_todos);    \
        return th_failures == 0 ? 0 : 1;                                                 \
    } while (0)

#endif /* TEST_HARNESS_H */
