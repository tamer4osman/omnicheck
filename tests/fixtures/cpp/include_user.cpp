// The import-channel fixture (SPRINT-21): the quoted include resolves
// (include local_util.h -- the root-default routed header, an edge to
// the analyzed file), the angle include stays system-unresolved
// (recorded, no edge), the macro path records NOTHING (not a static
// payload), the second quoted include is unresolved (missing.h is not
// analyzed).
#include "local_util.h"

#include <string>
#include HEADER_PATH
#include "missing.h"

int user_main(void) {
    return local_util(4);
}
