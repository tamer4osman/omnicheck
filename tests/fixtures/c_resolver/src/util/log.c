// Same-directory include from a second directory: log.c includes its own
// header -- the resolution is relative to log.c's directory (util/).
#include "log.h"

int log_line(const char *msg) {
    return (int)sizeof(msg);
}
