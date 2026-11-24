// OC008 duplicate-code fixture (SPRINT-11): byte-identical body of
// DupA.filter (see the sibling file's header comment for the split rule).
package com.example.dup;

public class DupB {

    public int filter(int[] values) {
        int out = 0;
        for (int v : values) {
            if (v == 0) {
                continue;
            }
            out += v;
            if (out > 8 && v == 9) {
                break;
            }
        }
        return out;
    }
}
