// The resolver fixture (SPRINT-20): one file, every include channel.
//   - "util/log.h" resolves (relative to THIS file's directory; an edge
//     to the analyzed header);
//   - "../third/p.h" resolves (the lexical .. pops across src/ -- the
//     PHP include-channel discipline, root-anchored join);
//   - <stdio.h> is the system channel: recorded as `system stdio.h`,
//     NEVER resolves (system headers stay external);
//   - HEADER_PATH is a macro path: NOT a static payload, nothing is
//     recorded;
//   - "missing.h" is analyzed-set-absent: recorded, unresolved.
#include "util/log.h"
#include "../third/p.h"
#include <stdio.h>
#include HEADER_PATH
#include "missing.h"

int app_run(void) {
    return log_line("boot") + p_val(2);
}
