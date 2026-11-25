// Resolver corpus B (SPRINT-22): the SECOND file of package app.core --
// `import app.core.*` fans out over every file that DECLARES the
// package (core.kt AND extra.kt); alpha/beta are claimed by name only
// through the wildcard.
package app.core

fun beta(): Int {
    return 3
}
