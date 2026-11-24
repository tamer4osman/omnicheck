<?php
// The PHPUnit-shaped runner file: the PHP pack registers NO test NAME
// conventions in v1 (a *_test.php name claims nothing), so the expected
// zero in-degree here IS OC006 material -- the honest contract of
// docs/LANGUAGES.md PHP section 6.
use PHPUnit\Framework\TestCase;

final class MathXTest extends TestCase
{
    public function testRollupSmoke(): void
    {
        $this->assertSame(4, rollup(1, [1], []));
    }
}
