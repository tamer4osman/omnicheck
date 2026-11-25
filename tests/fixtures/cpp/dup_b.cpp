// The OC008 duplicate-code pair, second copy (SPRINT-21): the same normalized block
// in two files (one group, two locations -- the SPRINT-20 convention).
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
