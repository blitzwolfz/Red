// Numbers: rounding, logarithms, trigonometry and randomness.

print(round(2.4), round(2.5), round(2.6));    // expect: 2 3 3
print(round(-2.4), round(-2.5), round(-2.6)); // expect: -2 -3 -3
print(sign(-7), sign(0), sign(7));            // expect: -1 0 1

print(exp(0));        // expect: 1
print(log(1));        // expect: 0
print(log(E));        // expect: 1
print(log(8, 2));     // expect: 3
print(log(1000, 10)); // expect: 3
print(log(81, 3));    // expect: 4

print(hypot(3, 4));  // expect: 5
print(hypot(5, 12)); // expect: 13

print(sin(0), cos(0), tan(0));    // expect: 0 1 0
print(round(sin(PI / 2)));        // expect: 1
print(asin(0), acos(1), atan(0)); // expect: 0 0 0
print(atan(1, 1) == PI / 4);      // expect: true
print(atan(1, -1) == PI * 3 / 4); // expect: true

print(PI > 3.14 and PI < 3.15); // expect: true
print(E > 2.71 and E < 2.72);   // expect: true

// A class of mistakes that would otherwise give nan quietly.
try {
  log(-1);
} catch (e: "domain") {
  print(e.message); // expect: log() of a negative number: -1.
}
try {
  acos(2);
} catch (e: "domain") {
  print(e.message); // expect: acos() needs a number from -1 to 1, got 2.
}

// rand() gives a fraction below one. rand(n) and rand(a, b) give whole
// numbers, and leave out the upper end so that rand(n) indexes an array
// of n things.
let inRange = true;
let whole = true;
for (let i in range(0, 200)) {
  const fraction = rand();
  if (fraction < 0 or fraction >= 1) { inRange = false; }
  const small = rand(4);
  if (small < 0 or small >= 4) { inRange = false; }
  if (small != floor(small)) { whole = false; }
  const between = rand(10, 20);
  if (between < 10 or between >= 20) { inRange = false; }
}
print(inRange, whole); // expect: true true

// A seed makes a run repeatable, which is what lets a test use
// randomness at all.
rand_seed(1234);
const first = [];
for (let i in range(0, 10)) { first.push(rand(1000)); }
rand_seed(1234);
const again = [];
for (let i in range(0, 10)) { again.push(rand(1000)); }
print(first.join(",") == again.join(",")); // expect: true

rand_seed(99);
const other = [];
for (let i in range(0, 10)) { other.push(rand(1000)); }
print(first.join(",") == other.join(",")); // expect: false

try {
  rand(5, 5);
} catch (e) {
  print(e.message); // expect: rand() needs a range with something in it, got 5 to 5.
}
