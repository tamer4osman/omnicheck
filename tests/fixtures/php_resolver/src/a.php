<?php

// The include cycle (OC003: one group of 2 files). Each side carries a
// whole-file claim (bind NULL, the Ruby require discipline).
include_once 'b.php';

function a_side(): int
{
    return 1;
}
