// Drives the a.h/b.h include cycle: one edge into each header (the
// cycle itself lives between the two headers).
#include "a.h"
#include "b.h"

int cycle_driver(void) {
    return a_api() + b_api();
}
