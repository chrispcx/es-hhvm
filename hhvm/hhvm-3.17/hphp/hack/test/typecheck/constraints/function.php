<?hh // strict

interface Constraint<T as num> {}

function foo(Constraint<?int> $c): void {}
