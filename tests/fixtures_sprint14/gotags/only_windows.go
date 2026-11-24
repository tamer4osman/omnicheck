//go:build windows

package main

func WindowsOnly() int {
	// TODO: would fire OC002 if this file were analyzed (it must not be)
	return 1
}
