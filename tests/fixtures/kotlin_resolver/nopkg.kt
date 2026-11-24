// Resolver corpus B (SPRINT-22): the DEFAULT package -- a Kotlin file
// may omit the package clause entirely; `import standalone` (the
// one-segment specifier) claims the default package's declared symbol.
fun standalone(): Int {
    return 9
}
