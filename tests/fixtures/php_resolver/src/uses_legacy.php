<?php

// The PSR-0 channel: Acme\Legacy\Legacy keeps the FULL name path under
// the mapping dir (lib/Acme/Legacy/Legacy.php), unlike PSR-4 which
// strips the prefix.
use Acme\Legacy\Legacy;

function legacy_boot(): string
{
    return Legacy::name();
}
