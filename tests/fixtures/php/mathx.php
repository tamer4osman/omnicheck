<?php
declare(strict_types=1);

// MathX is export surface (a top-level function). The comp-35 function is
// the hand-counted decision inventory of docs/LANGUAGES.md PHP section 4
// -- 34 decisions, one per recorded kind (the Ruby comp-35 analogue: ONE
// decision per case_statement, a default arm is the DISTINCT
// default_statement and counts 0; ONE decision per match arm, a comma
// condition list is ONE arm; elseif is its own else_if_clause).
//
// Decisions (34): if(1) + elseif(2) + while(1) + do(1) + for(1)
// + foreach(1) + case(5) + match_conditional(2) + match_default(1)
// + catch(2) + conditional(5, full and short ternary are the same node)
// = 22 kind decisions, plus &&(6) + ||(4) + and(1) + or(1) = 12 operator
// decisions -> 34 total, complexity 35. `??` counts 0 (a value
// operator, not a branch); throw/goto/exit/yield count 0 (the standing
// throw-not-a-branch rule). The definition extent stays under the
// OC001.max_lines default (60) so OC001 fires exactly once on this
// corpus -- the longfn.php fixture.
function rollup(int $x, array $ys, array $rows): int
{
    $total = 0;
    if ($x > 0 && count($ys) > 0 && !empty($rows) && $x < 1000) {
        $base = 1;
    } elseif ($x === 0 || $ys === []) {
        $base = 2;
    } elseif ($x < 0 && $x > -100) {
        $base = 3;
    } else {
        $base = 4;
    }
    $i = 0;
    while ($i < 3 && $base > 0) {
        $total += $base;
        $i++;
    }
    do {
        $total++;
        $base--;
    } while ($base > 0 || $total < 0);
    for ($j = 0; $j < 2; $j++) {
        $total += $j;
    }
    foreach ($ys as $k => $v) {
        $total += ($k === 0) ? 1 : 2;
    }
    switch ($base) {
        case 1: $total += 10; break;
        case 2: $total += 15; break;
        case 3: $total += 20; break;
        case 4: $total += 30; break;
        case 5: $total += 40; break;
        default: $total += 0;
    }
    try {
        $m = match (true) {
            $x > 100 => 'big',
            $x > 10 || $x > 5 => 'mid',
            default => 'small',
        };
        $total += strlen($m);
    } catch (TypeError $e) {
        $total += 1;
    } catch (Throwable | RuntimeException $e2) {
        $total += 2;
    } finally {
        $base = $base ?: 1;
    }
    $flag = ($total > 10) ? ($x and $base ? 1 : 0) : 0;
    $alt = $flag or $x || $base && $total > 2 ? 1 : 2;
    return $total + ($alt ?? 0);
}

function mathx_helper(array $values): int
{
    return array_sum($values);
}
