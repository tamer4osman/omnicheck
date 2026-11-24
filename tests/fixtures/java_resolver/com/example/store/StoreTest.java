// Resolver corpus B: a test-convention file INSIDE the store package.
// The Surefire runner convention (*Test.java) claims it as an entry
// (never OC006 material), and the wildcard import from Main.java DOES
// fan out to it -- a Java package claim covers the whole analyzed
// package, test files included (§Java.5; the deliberate contrast with
// Go's _test.go exclusion).
package com.example.store;

public class StoreTest {

    public String name() {
        return "store-test";
    }
}
