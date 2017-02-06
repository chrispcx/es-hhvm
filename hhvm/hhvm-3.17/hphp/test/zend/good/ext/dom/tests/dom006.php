<?php

Class books extends domDocument {
    function addBook($title, $author) {
        $titleElement = $this->createElement("title");
        $titleElement->appendChild($this->createTextNode($title));
        $authorElement = $this->createElement("author");
        $authorElement->appendChild($this->createTextNode($author));

        $bookElement = $this->createElement("book");

        $bookElement->appendChild($titleElement);
        $bookElement->appendChild($authorElement);
        $this->documentElement->appendChild($bookElement);
    }
   
}

$dom = new books;

$dom->load(dirname(__FILE__)."/book.xml");
$dom->addBook("PHP de Luxe", "Richard Samar, Christian Stocker");
print $dom->saveXML();
