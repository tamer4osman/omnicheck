// The OC003 cycle fixture (SPRINT-20): two headers including each other
// -- an include cycle between analyzed files (the include-cycle shape
// the C ecosystem actually produces, guard macros notwithstanding).
#ifndef A_H
#define A_H

#include "b.h"

int a_api(void);

#endif
