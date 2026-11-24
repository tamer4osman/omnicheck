<?php

declare(strict_types=1);

namespace Acme\Shop;

use Acme\Shop\Contracts\Mapper;
use Acme\Shop\Store\Types;
use Acme\Shop\{Product, Order as O};
use function Acme\Shop\helpers\sum;
use Psr\Log\LoggerInterface;
use Doctrine\ORM\Query;

// The resolver corpus class: every use above is a record. The five
// Acme\Shop-prefixed uses resolve through the ROOT composer.json psr-4
// mapping (the grouped form expands; the alias is local sugar and never
// narrows the marking). The Psr\... and Doctrine\... uses match no
// composer prefix and stay recorded-and-UNRESOLVED (external, the Go
// module-boundary precedent).
final class Shop implements Mapper
{
    public function render(Types $types, Product $p, LoggerInterface $log): string
    {
        $lines = [];
        foreach ($types->all() as $t) {
            if ($t->active || $p->stocked()) {
                $lines[] = $t->label;
            }
        }
        return count($lines) > 0 ? implode(',', $lines) : 'empty';
    }

    public function map(array $row): array
    {
        return array_map(static fn ($v) => (string) $v, $row);
    }
}
