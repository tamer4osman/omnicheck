// Drives the a.hpp/b.hpp include cycle: one edge into each header.
#include "a.hpp"
#include "b.hpp"

int cycle_driver(void) {
    return a_api() + b_api();
}
