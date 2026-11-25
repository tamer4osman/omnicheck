<?php

declare(strict_types=1);

namespace Acme\Shop;

final class Product
{
    public function stocked(): bool
    {
        return true;
    }
}
