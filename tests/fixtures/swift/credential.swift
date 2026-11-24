// The OC011 fixture (SPRINT-23): a hardcoded credential-shaped value in
// a top-level property -- the secret-pattern pass fires on the
// as-written source.
let password = "hunter2-super-secret-value"

func authenticate(user: String) -> Bool {
    return user == "admin"
}
