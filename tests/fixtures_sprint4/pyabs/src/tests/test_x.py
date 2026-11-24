"""Own package root misses; the analysis-root fallback resolves."""
import top


def test_top():
    return top.run()
