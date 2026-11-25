package beta

// Grade classifies n with six decision points (cyclomatic 6).
func Grade(n int) string {
	if n < 0 {
		return "neg"
	}
	if n == 0 {
		return "zero"
	}
	if n < 10 {
		return "low"
	}
	if n < 100 {
		return "mid"
	}
	if n < 1000 {
		return "high"
	}
	return "max"
}
