// OC011 secret-pattern fixture (SPRINT-11): the same documented example
// literal as the Go spike's cfg fixture (AWS's documentation example key
// id) so OC011 fires deterministically and exactly once -- the
// sibling-identity contract folds byte-identical matches. No real
// credential is present.
package cfg;

public class Secrets {

    public static final String AWS_ACCESS_KEY_ID = "AKIAIOSFODNN7EXAMPLE";
}
