// Compound assignment on every kind of target.

let n = 10;
n += 5; print(n); // expect: 15
n -= 3; print(n); // expect: 12
n *= 2; print(n); // expect: 24
n /= 6; print(n); // expect: 4
n %= 3; print(n); // expect: 1

// Strings join with +=.
let text = "a";
text += "b";
text += "c";
print(text); // expect: abc

// Globals, locals and captured variables all work.
let total = 0;
fun addToTotal(x) { total += x; }
addToTotal(3);
addToTotal(4);
print(total); // expect: 7

fun makeAccumulator() {
  let sum = 0;
  return fun (x) { sum += x; return sum; };
}
const accumulate = makeAccumulator();
accumulate(10);
print(accumulate(5)); // expect: 15

// Fields.
class Counter {
  init() { this.count = 0; }
}
const counter = Counter();
counter.count += 4;
counter.count *= 3;
print(counter.count); // expect: 12

// Array elements.
const numbers = [1, 2, 3];
numbers[0] += 10;
numbers[2] *= 5;
print(numbers); // expect: [11, 2, 15]

// Map entries, including one that starts out missing.
const scores = {"a": 1};
scores["a"] += 9;
print(scores["a"]); // expect: 10

// The subject of a compound assignment is evaluated once.
let calls = 0;
fun pick(items) { calls += 1; return items; }
const target = [0];
pick(target)[0] += 7;
print(target[0], calls); // expect: 7 1
