#include "mini_libc.h"
#include "wallclock.h"

int main(void) {
    CivilTime now;
    char line[48];
    if (wallclock_now(&now, 0, 0) != 0) {
        fprintf(2, "date: wall clock unavailable\n");
        return 1;
    }
    if (time_format(line, sizeof(line), TIME_FORMAT_LONG, &now) < 0) {
        fprintf(2, "date: cannot format time\n");
        return 1;
    }
    printf("%s\n", line);
    return 0;
}
