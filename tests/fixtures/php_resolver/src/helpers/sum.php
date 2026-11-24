<?php

declare(strict_types=1);

namespace Acme\Shop\helpers;

// Resolved through `use function Acme\Shop\helpers\sum` -- function uses
// ride the same classmap discipline when a prefix matches.
function sum(array $xs): int
{
    $acc = 0;
    foreach ($xs as $x) {
        $acc += $x;
    }
    return $acc;
}
