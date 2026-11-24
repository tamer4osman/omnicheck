// The OC001 rollup fixture (SPRINT-20): the hand-counted decision
// inventory of docs/LANGUAGES.md C section 4 -- every decision kind in
// one function, counted by construction and pinned by test_sprint20:
// comp(rollup) = 1 + 11 (if + else-if nested if_statement + while + do +
// for + three case arms with a value + the ternary + && + ||; the
// default arm and the bare else_clause count 0). The whole definition
// is the span; prototypes are NOT symbols.
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
    return t || n;
}

int one(void) {
    return 1;
}
