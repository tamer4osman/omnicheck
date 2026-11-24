// Decoy manifest (SPRINT-10 acceptance gate): the resolver never reads
// go.mod and never strips module prefixes (docs/LANGUAGES.md §Go.5), so
// the specifier "mycorp/store" in cmd/main.go stays unresolved even
// though store/ exists in this tree. Stripping the prefix below would
// make it resolve -- the gate asserts it does not.
module mycorp

go 1.22
