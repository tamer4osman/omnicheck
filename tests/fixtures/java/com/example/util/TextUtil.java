// Corpus A (SPRINT-11): the decision-kind fixture. The `describe` method
// carries exactly 24 decisions (complexity 25), one per contract kind of
// §Java.4 -- each decision is annotated with its ordinal so the gate can
// pin the exact number. Switch forms: a fallthrough switch with THREE
// case-labelled groups and a default-only group (which adds NO decision,
// §Java.4), and an arrow switch whose rules count (including the
// default rule). The text block pins the §Java.1 grammar caveat.
package com.example.util;

import java.util.Set;

public class TextUtil {

    // TODO: retire the mode parameter (OC002 fires once, here).
    public String describe(int n, String mode) { // def line 19
        String result = "";
        if (n < 0) {                                    // 1 if_statement
            result = "negative";
        } else if (n == 0) {                            // 2 if_statement (else-if)
            result = "zero";
        } else {
            for (int i = 0; i < n; i++) {               // 3 for_statement
                result += i;
            }
        }
        int[] items = {1, 2, 3};
        for (int it : items) {                          // 4 enhanced_for_statement
            if (it % 2 == 0) {                          // 5 if_statement
                result += "even";
            }
        }
        int w = 0;
        while (w < 3) {                                 // 6 while_statement
            w++;
        }
        do {                                            // 7 do_statement
            w--;
        } while (w > 0);
        String flag = (n > 10) ? "big" : "small";       // 8 ternary_expression
        boolean combo = (n > 0) && (mode != null);      // 9 &&
        combo = combo || (n == 7);                      // 10 ||
        try {
            result = result.substring(0, 1);
        } catch (RuntimeException e) {                  // 11 catch_clause
            result = "err";
        } catch (Exception e) {                         // 12 catch_clause
            result = "err2";
        }
        switch (mode) {                                 // statement switch: fallthrough groups
            case "a":                                   // 13 group with case label
                result += "A";
                break;
            case "b":                                   // 14 group with case label
            case "c":                                   // 15 group with case label
                result += "BC";
                break;
            default:                                    // default-only group: NO decision
                result += "D";
                break;
        }
        String arrow = switch (n) {                     // arrow switch: rules count
            case 1 -> "one";                            // 16 switch_rule
            case 2, 3 -> "few";                         // 17 switch_rule (one node, two labels)
            default -> "many"; };                       // 18 switch_rule (default rule counts)
        if (mode != null && mode.length() > 3) { result += "long"; } // 19 if + 20 &&
        boolean b2 = (n > 1) || (n < -1) || (mode == null); // 21 || + 22 ||
        int t = (w > 0) ? w : -w;                       // 23 ternary_expression
        for (int k = 0; k < items.length; k++) {        // 24 for_statement
            result += items[k];
        }
        return result + flag + arrow + t + combo + b2 + """
                [end]""";
    }

    public Set<String> names() {
        return Set.of("a", "b");
    }
}
