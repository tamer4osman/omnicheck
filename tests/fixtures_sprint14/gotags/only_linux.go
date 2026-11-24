//go:build linux

package main

func LinuxOnly() int {
	// TODO: tighten the linux-only fast path (analyzed: this file stays)
	return 1
}
