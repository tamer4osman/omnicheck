<?php
// The OC001 long function: a 63-line function definition (three lines over
// the default 60-line threshold). Lines are counted on the definition
// extent -- decl line through the closing brace. The body stays
// decision-light (plain loops and assignments, no branches) so OC004
// fires exactly once on this corpus -- the mathx.php comp-35 fixture.
function pipeline(array $records): array
{
    $out = [];
    $stage1 = [];
    foreach ($records as $r) {
        $stage1[] = trim((string) $r);
    }
    $stage2 = [];
    foreach ($stage1 as $s) {
        $stage2[] = strtolower($s);
    }
    $stage3 = [];
    foreach ($stage2 as $s) {
        $parts = explode(':', $s, 2);
        $stage3[$parts[0] ?? $s] = $parts[1] ?? '';
    }
    $stage4 = [];
    foreach ($stage3 as $k => $v) {
        $stage4[(string) $k] = preg_replace('/\s+/', ' ', (string) $v);
    }
    $stage5 = [];
    foreach ($stage4 as $k => $v) {
        $stage5[str_replace(' ', '_', $k)] = $v;
    }
    $stage6 = [];
    foreach ($stage5 as $k => $v) {
        $stage6[str_replace('.', '_', $k)] = $v;
    }
    ksort($stage6);
    $stage7 = [];
    foreach ($stage6 as $k => $v) {
        $stage7[$k] = (string) $v;
    }
    $stage8 = [];
    foreach ($stage7 as $k => $v) {
        $stage8[$k] = substr($v, 0, 255);
    }
    $stage9 = [];
    foreach ($stage8 as $k => $v) {
        $stage9[ucfirst($k)] = $v;
    }
    $stage10 = [];
    foreach ($stage9 as $k => $v) {
        $stage10['x_' . $k] = $v;
    }
    uksort($stage10, 'strcasecmp');
    $stage11 = [];
    foreach ($stage10 as $k => $v) {
        $stage11[$k] = strlen($v);
    }
    $stage12 = [];
    foreach ($stage11 as $k => $v) {
        $stage12[$k] = $v % 7;
    }
    $stage13 = [];
    foreach ($stage12 as $k => $v) {
        $stage13[$k] = $v * 3;
    }
    arsort($stage13);
    $stage14 = [];
    foreach ($stage13 as $k => $v) {
        $stage14[$k] = $v - 1;
    }
    $out = array_sum($stage14);
    return (int) $out;
}
