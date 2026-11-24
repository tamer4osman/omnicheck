// Package util holds the Go-pack spike fixtures (SPRINT-9, Track D wave 1).
package util

import "strings"

// TODO: replace the ad-hoc dispatcher below with a table-driven loader.
const Version = "0.1.0"

// LongFunction spans more than the OC001 line threshold and carries more
// than the OC004 complexity threshold -- both rules must fire here, and
// their fingerprints must be stable across runs (acceptance gate).
func LongFunction(mode int, items []string) string {
	var b strings.Builder
	switch mode {
	case 0:
		b.WriteString("zero")
	case 1:
		b.WriteString("one")
	case 2:
		b.WriteString("two")
	default:
		b.WriteString("many")
	}
	for i := 0; i < len(items); i++ {
		if len(items[i]) > 3 && strings.HasPrefix(items[i], "a") {
			b.WriteString(items[i])
		} else if len(items[i]) > 2 || strings.Contains(items[i], "z") {
			b.WriteString(strings.ToUpper(items[i]))
		}
		if i%2 == 0 && mode != 2 {
			b.WriteString("-even")
		}
		for j := 0; j < i%3; j++ {
			b.WriteString(".")
		}
	}
	switch {
	case len(items) == 0:
		b.WriteString("empty")
	case mode > 1:
		b.WriteString("heavy")
	}
	for _, it := range items {
		if strings.Contains(it, " ") || strings.HasPrefix(it, "x") {
			b.WriteString(" ")
		}
	}
	if mode > 0 && len(items) > 0 {
		state := 1
		_ = state
	}
	if mode > 1 && len(items) > 1 {
		b.WriteString("first")
	}
	if mode > 2 && len(items) > 2 {
		b.WriteString("second")
	}
	if mode > 3 || len(items) > 3 {
		b.WriteString("third")
	}
	if mode > 4 || len(items) > 4 {
		b.WriteString("fourth")
	}
	if mode > 5 && len(items) > 5 {
		b.WriteString("fifth")
	}
	if mode > 6 && len(items) > 6 {
		b.WriteString("sixth")
	}
	if mode > 7 || len(items) > 7 {
		b.WriteString("seventh")
	}
	if mode > 8 || len(items) > 8 {
		b.WriteString("eighth")
	}
	return b.String()
}

// Greeter demonstrates method_declaration handling (SPRINT-9): a method
// is a symbol with the same span contract, but NOT package surface.
type Greeter struct{}

// Hello is a method (receiver-scoped, not an export candidate).
func (g Greeter) Hello(name string) string {
	if name == "" {
		return "hi"
	}
	return "hi " + name
}

// Greet is exported package surface used by main.go.
func Greet(name string) string {
	return "hello " + name
}

// helper is unexported -- must NOT be an export candidate (the exported-
// name rule is case-based; OC005 does not consume candidates this sprint,
// but the surface records them for the resolver sprint).
func helper(name string) string {
	return "hey " + name
}
