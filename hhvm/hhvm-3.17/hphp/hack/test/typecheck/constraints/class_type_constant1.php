<?hh // strict

interface Constraint<T as num> {}

class InvalidConstraint {
  const type T = array<Constraint<string>>;
}
