// Corpus A (SPRINT-11): records, compact constructors, lambdas and
// anonymous classes (§Java.2). The lambda is NOT a symbol and the
// anonymous class is neither -- their decisions count toward the
// ENCLOSING method (inclusive-by-design); the anonymous class's own
// `run` method IS a symbol whose body also counts inward for `mid`.
package com.example.model;

import java.util.function.IntUnaryOperator;

public record Point(int x, int y) {

    public Point { // compact constructor: a symbol like any other
        if (x < 0) {
            throw new IllegalArgumentException("x must be >= 0");
        }
    }

    public int mid(int other) {
        IntUnaryOperator abs = v -> { // lambda: not a symbol
            if (v < 0) {              // counts toward mid (inclusive)
                return -v;
            }
            return v;
        };
        Runnable print = new Runnable() { // anonymous class: not a symbol
            public void run() {           // a symbol of its own
                if (x > 0) {              // counts toward run AND mid
                    System.out.print("x");
                }
            }
        };
        print.run();
        return abs.applyAsInt(other) + (y > other ? y : other); // ternary counts toward mid
    }
}
