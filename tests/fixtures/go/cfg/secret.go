// Package cfg holds the OC011 fixture: a credential-shaped literal.
package cfg

// AWSAccessKeyID holds AWS's documented example key id so OC011 fires
// deterministically in the spike gate: content-based supply rules work
// the day a language registers. (The literal appears exactly ONCE in this
// file -- the sibling-identity contract folds byte-identical matches, and
// the gate asserts one finding.)
const AWSAccessKeyID = "AKIAIOSFODNN7EXAMPLE"
