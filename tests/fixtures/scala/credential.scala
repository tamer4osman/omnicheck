// The OC011 fixture (SPRINT-24): a hardcoded credential-shaped value in
// a top-level val -- the secret-pattern pass fires on the as-written
// source.
package app.auth

val password = "hunter2-super-secret-value"

def authenticate(user: String): Boolean = {
  user == "admin"
}
