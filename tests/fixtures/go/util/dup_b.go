// OC008 duplicate-code fixture (SPRINT-9): the body block below is
// duplicated verbatim in dup_a.go -- identical package clause, identical
// doc comment, identical body; only the function name differs, which
// splits the normalized block exactly at the signature line so ONE
// duplicate group forms over the 11-line body.
package util

import "strings"

// DupTarget appears in two files; the normalized body block matches.
func DupB(values []string) []string {
        out := make([]string, 0, len(values))
        for _, v := range values {
                if v == "" {
                        continue
                }
                out = append(out, strings.TrimSpace(v))
                if len(out) > 8 && v == "stop" {
                        break
                }
        }
        return out
}
