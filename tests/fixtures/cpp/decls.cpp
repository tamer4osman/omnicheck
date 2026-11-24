// The OC005/OC001 boundary fixture (SPRINT-21): the prototype is NOT a
// symbol (the definition lives elsewhere), the member declaration inside
// the class is NOT a symbol (no body), the multi-declarator line names
// EACH declarator separately, and the static helper is a symbol but
// never module surface (internal linkage).
int add3(int a, int b, int c);

struct Gauge {
    int read(void);   // member declaration: not a symbol
    int level;
};

int a, b = 2, c[4];

int add3(int a, int b, int c) {
    return a + b + c;
}

static int hidden_helper(void) {
    return 7;
}
