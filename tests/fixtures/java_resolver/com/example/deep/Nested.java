// Resolver corpus B: the nested-class host. Main.java imports
// com.example.deep.Nested.Inner -- under the class-file model the only
// candidate file is com/example/deep/Nested/Inner.java, which does not
// exist; v1 never falls back to Nested.java (deciding that Inner is
// nested in Nested would be a guess, §Java.5), so THAT import stays
// unresolved. The single-type import from Helper.java (com.example.deep.Nested)
// resolves and gives this file in-degree 1 -- pinning the OC005 verdict
// on the never-imported-by-name export.
package com.example.deep;

public class Nested {

    public static class Inner {

        public static final String NAME = "inner";
    }

    public static String tag() {
        return "deep";
    }
}
