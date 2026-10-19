/* The host program that stands in for the hardware of tests/freestanding/firmware.gm: it provides
 * the serial port and the trap handler the firmware declares foreign, and calls its entry points. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void gm_entry(void);
extern unsigned char gm_crash(size_t i);

void uart_putc(unsigned char c) { putchar(c); }

void harness_trap(const unsigned char *msg, size_t len, unsigned line, unsigned col, size_t file_len) {
    printf("TRAP: %.*s (line %u, col %u, file name of %zu bytes)\n", (int)len, (const char *)msg, line, col, file_len);
    fflush(stdout);
    exit(3);
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "crash") == 0) {
        printf("in range: %d\n", gm_crash(2));
        printf("out of range: %d\n", gm_crash(9));
        printf("not reached\n");
        return 0;
    }
    gm_entry();
    fflush(stdout);
    return 0;
}
