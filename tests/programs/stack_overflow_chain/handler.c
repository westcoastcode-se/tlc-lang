#include <signal.h>
#include <string.h>
#include <unistd.h>

static void on_segv(int sig) {
    (void)sig;
    static const char text[] = "c handler";
    write(2, text, sizeof text - 1);
    _exit(7);
}

__attribute__((constructor)) static void install(void) {
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = on_segv;
    sigaction(SIGSEGV, &action, 0);
}
