// OC008 duplicate-code fixture (SPRINT-11): the body block below is
// duplicated verbatim in DupB.java -- identical package clause, identical
// doc comment, identical body; only the class and method names differ,
// which splits the normalized block exactly at the signature line so ONE
// duplicate group forms over the body. No imports: the corpus must stay
// edge-free (§Java.5 never-guess pins live in java_resolver).
package com.example.dup;

public class DupA {

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
