<?php
$dom = new DOMDocument;
$dom->formatOutput = 1;
var_dump($dom->formatOutput);

$dom2 = clone $dom;
var_dump($dom2->formatOutput);
?>
