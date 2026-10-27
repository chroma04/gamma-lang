/* The C side of the gammac fuzz targets.
 *
 * The C that gammac generates for a program defines `main`. The build renames it to
 * `gm_program_main` (-Dmain=gm_program_main) so that this file can own the real `main`: it
 * starts the Gamma program, whose `main` sets the program up (the runtime, the ambient
 * handlers, a large stack) and calls `gm_fuzz_run`, which runs libFuzzer's driver on the
 * same thread. libFuzzer then calls the Gamma entry function for every input.
 *
 * The Gamma program runs on a thread of its own, but libFuzzer's timeout (SIGALRM) only
 * acts on the thread that runs the driver. So the original thread blocks the signal
 * before the program starts, and `gm_fuzz_run` unblocks it on the driver's thread. */

#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerRunDriver(int *argc, char ***argv, int (*callback)(const uint8_t *data, size_t size));
int gm_program_main(int argc, char **argv);

static int saved_argc;
static char **saved_argv;

int gm_fuzz_run(int (*callback)(const uint8_t *data, size_t size)) {
    sigset_t alarm;
    sigemptyset(&alarm);
    sigaddset(&alarm, SIGALRM);
    pthread_sigmask(SIG_UNBLOCK, &alarm, NULL);
    int argc = saved_argc;
    char **argv = saved_argv;
    return LLVMFuzzerRunDriver(&argc, &argv, callback);
}

int main(int argc, char **argv) {
    saved_argc = argc;
    saved_argv = argv;
    sigset_t alarm;
    sigemptyset(&alarm);
    sigaddset(&alarm, SIGALRM);
    pthread_sigmask(SIG_BLOCK, &alarm, NULL);
    return gm_program_main(argc, argv);
}
