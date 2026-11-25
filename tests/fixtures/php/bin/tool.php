#!/usr/bin/env php
<?php
// The bin/ script: the pack's entry-point glob claims this file (the
// **/bin/* runner-script convention). PHP has NO shebang hint by
// contract: the #! line outside the PHP block is template TEXT and is
// inert to every analysis -- the claim here comes from the pack data,
// never from the shebang (docs/LANGUAGES.md PHP section 6).
final class Tool
{
    public function run(string $name): string
    {
        return json_encode(['name' => $name]);
    }
}

echo (new Tool())->run('tool'), PHP_EOL;
