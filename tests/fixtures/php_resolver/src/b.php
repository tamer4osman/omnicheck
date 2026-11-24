<?php

// The include cycle (OC003: one group of 2 files).
include_once 'a.php';

function b_side(): int
{
    return 2;
}
