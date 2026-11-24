<?php

declare(strict_types=1);

namespace Acme\Shop;

// Imported through the grouped use form (`use Acme\Shop\{Product, Order
// as O}`); the alias O is the importing file's local name and never
// narrows the OC005 marking (the C# alias discipline).
final class Order
{
    public function total(): int
    {
        return 0;
    }
}
