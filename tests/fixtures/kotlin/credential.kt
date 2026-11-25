// The OC011 fixture (SPRINT-22): a hardcoded credential-shaped value in
// a top-level property -- the secret-pattern pass fires on the
// as-written source.
package app.auth

val password = "hunter2-super-secret-value"

fun authenticate(user: String): Boolean {
    return user == "admin"
}
