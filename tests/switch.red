// switch compares a value against each case. There is no fall through,
// so no break is needed.

fun describe(n) {
  switch (n) {
    case 0: return "zero";
    case 1: return "one";
    case 2, 3, 4: return "a few";
    default: return "many";
  }
}
print(describe(0));              // expect: zero
print(describe(1));              // expect: one
print(describe(3));              // expect: a few
print(describe(9));              // expect: many

// It works on strings too, and the subject is evaluated once.
let calls = 0;
fun subject() { calls += 1; return "b"; }
switch (subject()) {
  case "a": print("first");
  case "b": print("second");     // expect: second
  default: print("other");
}
print(calls);                    // expect: 1

// With no match and no default, nothing runs.
let ran = false;
switch (99) {
  case 1: ran = true;
}
print(ran);                      // expect: false

// Cases can hold several statements and their own declarations.
switch (2) {
  case 2:
    let doubled = 2 * 2;
    print("doubled ${doubled}"); // expect: doubled 4
    print("still here");         // expect: still here
  default:
    print("not reached");
}

// Case values are expressions, not just literals.
const target = 7;
switch (3 + 4) {
  case target: print("computed match");   // expect: computed match
  default: print("no");
}

// A switch inside a loop: break belongs to the loop, because a case
// never falls through.
let collected = [];
for (let i in [1, 2, 3, 4]) {
  switch (i) {
    case 3: break;
    default: collected.push(i);
  }
}
print(collected);                // expect: [1, 2]

// Equality is the same as ==, so a nil case matches nil.
switch (nil) {
  case nil: print("nil matched");         // expect: nil matched
  default: print("no");
}
