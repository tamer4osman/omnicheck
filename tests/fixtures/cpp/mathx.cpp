// The comp-35 fixture (SPRINT-21): the hand-counted decision
// inventory -- 34 decisions, one per recorded kind (docs/LANGUAGES.md
// C++ section 4): nine if/else-if pairs and two guard ifs (20
// if_statements), four while, two do, two for, one C++11 range-for,
// two ternaries, one catch handler, and the two && tokens of the
// guard ifs; every else_clause and the default arm count 0. The
// definition extent stays under the OC001 default (60) so OC001 fires
// exactly once on this corpus -- the longfn.cpp fixture.
#include <string>
int sum26(int n) {
    if (n == 0) { n = 0 + 1; } else if (n == -0) { n = -0 - 1; }
    if (n == 1) { n = 1 + 1; } else if (n == -1) { n = -1 - 1; }
    if (n == 2) { n = 2 + 1; } else if (n == -2) { n = -2 - 1; }
    if (n == 3) { n = 3 + 1; } else if (n == -3) { n = -3 - 1; }
    if (n == 4) { n = 4 + 1; } else if (n == -4) { n = -4 - 1; }
    if (n == 5) { n = 5 + 1; } else if (n == -5) { n = -5 - 1; }
    if (n == 6) { n = 6 + 1; } else if (n == -6) { n = -6 - 1; }
    if (n == 7) { n = 7 + 1; } else if (n == -7) { n = -7 - 1; }
    if (n == 8) { n = 8 + 1; } else if (n == -8) { n = -8 - 1; }
    while (n > 100) { n--; }
    while (n > 100) { n--; }
    while (n > 100) { n--; }
    while (n > 100) { n--; }
    do { n++; } while (n < -100);
    do { n++; } while (n < -100);
    for (int k0 = 0; k0 < 2; k0++) { n += k0; }
    for (int k1 = 0; k1 < 2; k1++) { n += k1; }
    int arr[2] = {1, 2};
    for (int v : arr) { n += v; }
    n = n > 0 ? 1 : n;
    n = n == 0 ? 2 : n;
    try { n = n + 1; } catch (const long &e) { n = (int)e; }
    if (n > 0 && n < 10) { n = 10; }
    if (n > -10 && n < 0) { n = -10; }
    (void)sizeof(std::string);
    return n;
}
