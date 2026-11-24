// The OC005/OC001 boundary fixture (SPRINT-20): three prototypes are NOT
// symbols (the definition lives elsewhere; counting both would double
// every function and give OC007 two anchors), the multi-declarator line
// names EACH declarator separately, and the static helper is a symbol
// but never module surface (internal linkage).
int add3(int a, int b, int c);
extern unsigned flags;
int a, b = 2, c[4];

int add3(int a, int b, int c) {
    return a + b + c;
}

static int hidden_helper(void) {
    return 7;
}
