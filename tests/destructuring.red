// Patterns bind several names at once, from arrays, maps and instances.

let [a, b] = [1, 2];
print(a, b); // expect: 1 2

// A pattern may be longer than what it matches. Missing parts are nil,
// the same way a missing map key is.
let [x, y, z] = [10, 20];
print(x, y, z); // expect: 10 20 nil

// A rest binding takes whatever is left, always as an array.
let [head, ...tail] = [1, 2, 3, 4];
print(head, tail); // expect: 1 [2, 3, 4]
let [only, ...none] = [9];
print(only, none); // expect: 9 []

// Map patterns bind by field name, and can rename.
let {name, age} = {"name": "ann", "age": 31};
print(name, age); // expect: ann 31
let {name: who} = {"name": "bob"};
print(who); // expect: bob
let {absent} = {};
print(absent); // expect: nil

// Instances work the same way, reading fields rather than keys. A
// pattern never picks up a method by accident.
class Point {
  init(x, y) { this.x = x; this.y = y; }
  len() { return 99; }
}
let {x: px, y: py} = Point(3, 4);
print(px, py); // expect: 3 4
let {len} = Point(1, 2);
print(len); // expect: nil

// Patterns nest, in both directions.
let [[p, q], {z}] = [[1, 2], {"z": 3}];
print(p, q, z); // expect: 1 2 3
let {outer: [m, n]} = {"outer": [7, 8]};
print(m, n); // expect: 7 8

// const patterns bind constants.
const [c1, c2] = [1, 2];
print(c1, c2); // expect: 1 2

// A pattern works inside a function, a block and a loop, and the slots
// are cleaned up each time round.
fun swap(pair) {
  let [first, second] = pair;
  return [second, first];
}
print(swap([1, 2])); // expect: [2, 1]

{
  let [inner] = [42];
  print(inner); // expect: 42
}

let total = 0;
for (let i in range(0, 100)) {
  let [lo, hi] = [i, i + 1];
  let {step} = {"step": 1};
  total += lo + hi + step;
}
print(total); // expect: 10100

// for-in can take a pattern, which is what makes walking entries read
// well.
const ages = {"ann": 31, "bob": 25};
let pairs = [];
for (let [who2, years] in ages.entries()) {
  pairs.push("${who2}=${years}");
}
print(pairs.sort()); // expect: ["ann=31", "bob=25"]

for (let [first2, second2] in [[1, 2], [3, 4]]) {
  print(first2 + second2);
}
// expect: 3
// expect: 7

// A closure made in the body captures that pass's bindings.
let readers = [];
for (let [value] in [[1], [2]]) {
  readers.push(fun () { return value; });
}
print(readers[0](), readers[1]()); // expect: 1 2

// Strings can be taken apart by position too.
let [firstLetter, secondLetter] = "hi";
print(firstLetter, secondLetter); // expect: h i

// Destructuring something with no elements is an error that names the
// type, rather than a confusing failure further on.
try {
  let [broken] = 42;
} catch (e: "type") {
  print(e.message); // expect: Cannot take elements from a value of type number.
}
try {
  let {broken} = 42;
} catch (e: "type") {
  print(e.message); // expect: Cannot take fields from a value of type number.
}
try {
  let [...spread] = {};
} catch (e: "type") {
  print(e.message); // expect: A rest pattern needs an array, got map.
}
