// The OC004 rollup fixture (SPRINT-21): the hand-counted decision
// inventory of docs/LANGUAGES.md C++ section 4 -- every decision kind in
// one function, counted by construction and pinned by test_sprint21:
// comp(rollup) = 1 + 13 (if + else-if nested if_statement + while + do +
// for + the C++11 range-for + three case arms with a value + the ternary
// + one catch handler + && + ||; the default arm and the bare else count
// 0). The whole definition is the span; prototypes are NOT symbols.
#include <string>

int rollup(int n) {
    if (n > 0) {
        return 1;
    } else if (n < 0) {
        return -1;
    } else {
        return 0;
    }
    while (n > 0) {
        n--;
    }
    do {
        n++;
    } while (n < 5);
    for (int i = 0; i < n && i < 10; i++) {
        n--;
    }
    int arr[4] = {1, 2, 3, 4};
    for (int v : arr) {
        n += v;
    }
    switch (n) {
    case 1:
        break;
    case 2:
    case 3:
        break;
    default:
        break;
    }
    int t = n > 0 ? 1 : 0;
    try {
        t = t + n;
    } catch (const int &e) {
        t = e;
    }
    (void)sizeof(std::string);
    return t || n;
}

int one(void) {
    return 1;
}
