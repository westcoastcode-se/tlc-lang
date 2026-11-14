#include <stdarg.h>

int pick(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int total = 0;
    for (int i = 0; i < n; ++i) {
        int value = va_arg(ap, int);
        if (value != 0 && value != 1) {
            total += 100;
        }
        total += value;
    }
    va_end(ap);
    return total;
}
