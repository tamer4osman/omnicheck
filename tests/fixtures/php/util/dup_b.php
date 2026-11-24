<?php
// The OC008 duplicate-code pair: the same 10-line normalized block in
// two files (one group, two locations -- the rust corpus convention).
class LedgerB
{
    public function firstPage(array $rows, array &$audit, array &$seen): int
    {
        $total = 0;
        foreach ($rows as $r) {
            if (!$r->valid) {
                continue;
            }
            $total += $r->value;
            $audit[] = $r->tag;
            $seen[$r->key] = true;
        }
        return $total;
    }
}
