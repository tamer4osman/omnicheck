// The OC005 surface fixture (SPRINT-20): tag DEFINITIONS (name AND body),
// typedefs and macros are module surface; the anonymous struct variable
// names only its declarator; `struct point pt;` below merely USES the
// tag and records nothing; enum constants (enumerator) are NOT surface
// v1 -- the documented boundary, pinned by test_sprint20.
struct point {
    int x;
    int y;
};

struct {
    int a;
} anon_var;

typedef struct point point_t;
typedef int (*handler)(void *, size_t);

enum color {
    RED,
    GREEN = 2,
    BLUE
};

#define MAX_TAGS 16
#define TAG(x) ((x) * 2)

union raw {
    unsigned u;
    void *p;
};

struct point pt;
