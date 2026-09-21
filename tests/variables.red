// let, const, shadowing and block scope.

let a = 1;
a = a + 1;
print(a); // expect: 2

const limit = 10;
print(limit); // expect: 10

{
  let a = "inner";
  print(a); // expect: inner
}
print(a); // expect: 2

// A declaration with no initializer starts at nil.
let empty;
print(empty); // expect: nil

// Annotations are parsed and ignored.
let typed: Int = 5;
print(typed); // expect: 5
