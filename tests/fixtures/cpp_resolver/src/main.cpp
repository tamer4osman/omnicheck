// The resolver fixture (SPRINT-21): one file, every include channel
// (the v1-cpp semantics are v1-c shared).
//   - "util/log.hpp" resolves (relative to THIS file's directory);
//   - "../third/p.hpp" resolves (the lexical .. pops across src/);
//   - <vector> is the system channel: recorded, NEVER resolves;
//   - HEADER_PATH is a macro path: nothing is recorded;
//   - "missing.hpp" is analyzed-set-absent: recorded, unresolved.
#include "util/log.hpp"
#include "../third/p.hpp"
#include <vector>
#include HEADER_PATH
#include "missing.hpp"

int app_run(void) {
    return log_line("boot") + p_val(2);
}
