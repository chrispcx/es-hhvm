//// newtype.php
<?hh // strict
newtype FBID = int;

//// usage.php
<?hh // strict

class C {
  const FBID mark = /* UNSAFE_EXPR */ 4;
}
