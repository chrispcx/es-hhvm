<?php
// reusing existing xml: http://cvs.php.net/viewvc.cgi/php-src/ext/dom/tests/dom.xml?view=co&content-type=text%2Fplain
// reusing existing dtd: http://cvs.php.net/viewvc.cgi/php-src/ext/dom/tests/dom.ent?view=co&content-type=text%2Fplain
$dom = new DOMDocument('1.0');
$dom->substituteEntities = true;
$dom->load(dirname(__FILE__).'/dom.xml');
var_dump($dom->validate());
?>
