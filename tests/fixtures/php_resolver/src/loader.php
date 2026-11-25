<?php

// The include/require channel: exactly one static string argument each.
// `require 'Shop.php'` joins relative to THIS file's directory (src/);
// `include_once 'store/../Contracts/Mapper.php'` pops lexically across
// the store/ segment (the lexical normalizer's .. handling). The __DIR__ concatenation and the interpolated
// include are the guard decoys -- they record NOTHING (the argument
// guard; docs/LANGUAGES.md PHP section 5).
require 'Shop.php';
include_once 'store/../Contracts/Mapper.php';
require_once __DIR__ . '/missing.php';
$name = 'dynamic';
include "store/{$name}.php";

function boot(): int
{
    return 1;
}
