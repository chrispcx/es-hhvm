<?php

class Foo  {
	public $bar = 1;
}
$f = new foo;
$f->dynProp = 'hello';
$f->dynProp2 = 'hello again';
echo new ReflectionObject($f);

?>
