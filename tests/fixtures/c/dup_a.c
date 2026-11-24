// The OC008 duplicate-code pair (SPRINT-20): the same normalized block
// in two files (one group, two locations -- the php corpus convention).
int page_sum(const int *rows, int n, int *audit, int *seen) {
    int total = 0;
    for (int i = 0; i < n; i++) {
        if (rows[i] > 0 && seen[i] == 0) {
            total += rows[i];
            audit[i] = 1;
            seen[i] = 1;
        }
    }
    return total;
}
