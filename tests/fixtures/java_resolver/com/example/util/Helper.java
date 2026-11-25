// Resolver corpus B: the util package (single-file). The wildcard claim
// from Main.java fans out to this file; the single-type import of
// Nested pins the OC005 path (Nested's export is never imported by name,
// but Nested IS imported -- in-degree 1, so OC005 judges it).
package com.example.util;

import com.example.deep.Nested;

public class Helper {

    public String name() {
        return Nested.tag() + "-helper";
    }
}
