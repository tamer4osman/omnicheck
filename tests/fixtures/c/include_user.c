// The import-channel fixture (SPRINT-20): the quoted include resolves
// (include local_util.h, an edge to the analyzed header), the angle
// include stays system-unresolved (recorded, no edge), the macro path
// records NOTHING (not a static payload), the second quoted include is
// unresolved (missing.h is not analyzed).
#include "local_util.h"

#include <stdio.h>
#include HEADER_PATH
#include "missing.h"

int user_main(void) {
    return local_util(4);
}
