<?php

declare(strict_types=1);

namespace Acme\Shop\Store;

final class Types
{
    /** @var list<object> */
    private array $all = [];

    public function add(object $t): void
    {
        $this->all[] = $t;
    }

    /** @return list<object> */
    public function all(): array
    {
        return $this->all;
    }

    public function label(): string
    {
        return 'types';
    }
}
