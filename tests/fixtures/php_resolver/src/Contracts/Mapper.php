<?php

declare(strict_types=1);

namespace Acme\Shop\Contracts;

interface Mapper
{
    public function map(array $row): array;
}
