<?php
// The OC002 fixture: exactly one marker comment, as written below.

// TODO: wire the repository into the container once the kernel lands.
class App
{
    public function boot(): bool
    {
        return true;
    }
}
