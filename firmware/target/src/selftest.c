#include "selftest.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "console.h"
#include "stm32g0xx.h"

/* tests/host/test_*.cのmainをCMakeで改名したもの */
int ut_crc32c_main(void);
int ut_dpll_main(void);
int ut_jetson_proto_main(void);
int ut_nmea_main(void);
int ut_time_main(int argc, char **argv);

int _write(int fd, const char *buf, int len);

/* newlibのstdout/stderrをUSB consoleへ流す。LFはCRLFにする */
int _write(int fd, const char *buf, int len)
{
    int start = 0;

    (void)fd;
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            console_write(buf + start, (size_t)(i - start));
            console_write("\r\n", 2u);
            start = i + 1;
        }
    }
    if (start < len) {
        console_write(buf + start, (size_t)(len - start));
    }
    return len;
}

void selftest_ut(const char *arg)
{
    char prog[] = "ut";
    char *argv[] = {prog, NULL};
    int failed_files = 0;

    (void)arg;
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    const uint32_t t0 = board_millis();
    printf("== test_crc32c\n");
    failed_files += ut_crc32c_main() != 0;
    printf("== test_dpll\n");
    failed_files += ut_dpll_main() != 0;
    printf("== test_jetson_proto\n");
    failed_files += ut_jetson_proto_main() != 0;
    printf("== test_nmea\n");
    failed_files += ut_nmea_main() != 0;
    printf("== test_time\n");
    failed_files += ut_time_main(1, argv) != 0;
    printf("UT on target: %s（失敗file %d、%lu ms、SYSCLK %lu Hz）\n", failed_files ? "FAILED" : "ALL PASSED",
           failed_files, (unsigned long)(board_millis() - t0), (unsigned long)SystemCoreClock);
}

void selftest_fault(const char *arg)
{
    if (strcmp(arg, "css") == 0) {
        con_printf("次の起動をHSE CSSの後と同じclock診断modeにしてresetする（resetで通常に戻る）\r\n");
        console_drain(50);
        g_fault_marker = BOARD_FAULT_MARKER_CSS;
        __DSB();
        NVIC_SystemReset();
    }
    con_printf("usage: fault css\r\n");
}
