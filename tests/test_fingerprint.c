#include "oc/fingerprint.h"
#include "testkit.h"

int main(void) {
    uint64_t h1 = oc_fingerprint_u64("OC001", "src/a.js", "bigFn", NULL);
    uint64_t h2 = oc_fingerprint_u64("OC001", "src/a.js", "bigFn", NULL);
    CHECK(h1 == h2); /* deterministic */

    CHECK(oc_fingerprint_u64("OC001", "src/a.js", "bigFn", NULL) !=
          oc_fingerprint_u64("OC002", "src/a.js", "bigFn", NULL));
    CHECK(oc_fingerprint_u64("OC001", "src/a.js", "bigFn", NULL) !=
          oc_fingerprint_u64("OC001", "src/b.js", "bigFn", NULL));
    CHECK(oc_fingerprint_u64("OC001", "src/a.js", "bigFn", NULL) !=
          oc_fingerprint_u64("OC001", "src/a.js", "otherFn", NULL));
    CHECK(oc_fingerprint_u64("OC001", "src/a.js", "ab", NULL) !=
          oc_fingerprint_u64("OC001", "src/a.js", "a", "b"));

    /* NULL snippet and empty snippet hash the same. */
    CHECK(oc_fingerprint_u64("R", "f", "a", NULL) == oc_fingerprint_u64("R", "f", "a", ""));

    char hex[OC_FINGERPRINT_HEX_LEN + 1];
    oc_fingerprint_hex(h1, hex);
    CHECK(strlen(hex) == OC_FINGERPRINT_HEX_LEN);
    for (size_t i = 0; i < OC_FINGERPRINT_HEX_LEN; i++)
        CHECK((hex[i] >= '0' && hex[i] <= '9') || (hex[i] >= 'a' && hex[i] <= 'f'));
    uint64_t back = 0;
    for (size_t i = 0; i < 8; i++) {
        char t[3] = {hex[2 * i], hex[2 * i + 1], 0};
        unsigned byte = 0;
        CHECK(sscanf(t, "%2x", &byte) == 1);
        back = (back << 8) | (unsigned char)byte;
    }
    CHECK(back == h1);

    char norm[128];
    oc_fingerprint_normalize("  TODO   fix   this  ", norm, sizeof(norm));
    CHECK_STR_EQ(norm, "TODO fix this");
    oc_fingerprint_normalize(NULL, norm, sizeof(norm));
    CHECK_STR_EQ(norm, "");
    char small[8];
    oc_fingerprint_normalize("abcdefghijk", small, sizeof(small));
    CHECK(strlen(small) <= 7);
    TEST_SUMMARY();
}
