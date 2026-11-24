package sig

func ArityBreak(a, b, c int) int {
	return a + b + c
}

func Typed(a string) int {
	return 0
}

func Reorder(b string, a int) string {
	return ""
}

func Variadic(a int, b int, rest ...int) int {
	return a + b
}
