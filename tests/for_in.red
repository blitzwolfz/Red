// for-in walks arrays, map keys and string characters.

for (let item in [10, 20, 30]) {
  print(item);
}
// expect: 10
// expect: 20
// expect: 30

// A map yields its keys, and the order is not defined, so the test
// sorts what it collected.
const ages = {"ann": 31, "bob": 25, "cal": 40};
let names = [];
for (let name in ages) {
  names.push("${name}=${ages[name]}");
}
print(names.sort());             // expect: ["ann=31", "bob=25", "cal=40"]

// A string yields its characters.
let letters = [];
for (let c in "abc") { letters.push(c); }
print(letters);                  // expect: ["a", "b", "c"]

// An empty subject runs the body zero times.
let ran = false;
for (let x in []) { ran = true; }
print(ran);                      // expect: false

// break and continue work, and leave the stack clean.
let kept = [];
for (let n in [1, 2, 3, 4, 5, 6]) {
  if (n % 2 == 0) { continue; }
  if (n > 4) { break; }
  kept.push(n);
}
print(kept);                     // expect: [1, 3]

// Nested loops each keep their own position.
let pairs = [];
for (let a in [1, 2]) {
  for (let b in ["x", "y"]) {
    pairs.push("${a}${b}");
  }
}
print(pairs);                    // expect: ["1x", "1y", "2x", "2y"]

// The loop variable is a fresh binding each time round, so a closure
// made in the body captures that iteration's value.
let readers = [];
for (let value in [1, 2, 3]) {
  readers.push(fun () { return value; });
}
print(readers[0](), readers[1](), readers[2]());   // expect: 1 2 3

// Walking a map is a snapshot of its keys, so adding entries during the
// walk does not disturb it.
const growing = {"a": 1};
let seen = 0;
for (let key in growing) {
  seen += 1;
  growing["added ${seen}"] = seen;
}
print(seen, growing.len());      // expect: 1 2

// Leaving a for-in from inside a try closes the handler.
for (let n in [1, 2, 3]) {
  try {
    if (n == 2) { break; }
  } catch (e) { print("wrong handler"); }
}
try {
  throw "clean";
} catch (e) {
  print(e.message);              // expect: clean
}

// Walking something that is not a sequence is an error.
try {
  for (let x in 42) { print(x); }
} catch (e) {
  print(e.message);              // expect: Cannot walk a value of type number.
}
