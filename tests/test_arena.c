#include "oc/arena.h"
#include "testkit.h"

int main(void) {
    oc_arena a;
    oc_arena_init(&a);

    void *p1 = oc_arena_alloc(&a, 1);
    void *p2 = oc_arena_alloc(&a, 33);
    CHECK(p1 && p2);
    CHECK(((uintptr_t)p1 & 15u) == 0);
    CHECK(((uintptr_t)p2 & 15u) == 0);

    /* Enough allocations to cross several blocks. */
    for (int i = 0; i < 5000; i++) {
        void *p = oc_arena_alloc(&a, (size_t)(i % 300) + 1);
        if (!p) {
            CHECK(0);
            break;
        }
        memset(p, 0xAB, (size_t)(i % 300) + 1);
    }
    CHECK(a.tail->next == NULL); /* invariant: tail is the newest block */

    char *s = oc_arena_strdup(&a, "hello arena");
    CHECK_STR_EQ(s, "hello arena");
    char *t = oc_arena_strndup(&a, "truncate-me", 8);
    CHECK_STR_EQ(t, "truncate");

    char *f = oc_arena_sprintf(&a, "n=%d name=%s", 42, "oc");
    CHECK_STR_EQ(f, "n=42 name=oc");

    CHECK(a.total_allocated > 5000);

    oc_arena_destroy(&a);
    /* After destroy all pointers above are invalid by design; ASan in CI
     * would flag any use-after-free or leak here. */
    TEST_SUMMARY();
}
