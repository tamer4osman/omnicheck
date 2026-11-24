<!doctype html>
<html>
<body>
<!-- The mixed php+html template: the HTML outside PHP blocks is
     text/text_interpolation nodes, inert to every analysis -- no
     symbol, no decision, no import ever comes from the prose. The
     if_statement between the interpolations parses clean (probe file
     4 of SPRINT-19). -->
<h1><?= htmlspecialchars($title) ?></h1>
<?php if ($ok): ?>
  <b>yes</b>
<?php elseif ($maybe): ?>
  <i>maybe</i>
<?php else: ?>
  <span>no</span>
<?php endif; ?>
<ul>
<?php foreach ($items as $item): ?>
  <li><?= htmlspecialchars((string) $item) ?></li>
<?php endforeach; ?>
</ul>
</body>
</html>
