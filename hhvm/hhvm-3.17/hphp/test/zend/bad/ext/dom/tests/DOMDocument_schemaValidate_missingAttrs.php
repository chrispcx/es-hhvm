<?php

$doc = new DOMDocument;

$doc->load(dirname(__FILE__)."/book-attr.xml");

$doc->schemaValidate(dirname(__FILE__)."/book.xsd");

foreach ($doc->getElementsByTagName('book') as $book) {
    var_dump($book->getAttribute('is-hardback'));
}

?>
