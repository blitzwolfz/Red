// try, catch, throw and the shape of caught errors.

try {
  throw "something broke";
} catch (e) {
  print(e.message); // expect: something broke
}

// Runtime faults are catchable and carry the same shape.
try {
  let bad = 1 / 0;
} catch (e) {
  print(e.message); // expect: Division by zero.
}

try {
  undefinedName();
} catch (e) {
  print(e.message); // expect: Undefined variable 'undefinedName'.
}

// Any value can be thrown. It is wrapped so catch always gets an error.
try {
  throw 42;
} catch (e) {
  print(e.message, e.payload); // expect: 42 42
}

// error() builds one explicitly, with an optional payload.
try {
  throw error("bad input", {"field": "age"});
} catch (e) {
  print(e.message, e.payload["field"]); // expect: bad input age
}

// An error carries the call stack from where it was raised.
fun deep() { throw "deep failure"; }
try {
  deep();
} catch (e) {
  print(e.trace.contains("deep")); // expect: true
}

// Unwinding crosses call frames and restores the stack.
fun thrower() { throw "from inside"; }
fun middle() { thrower(); return "not reached"; }
try {
  middle();
} catch (e) {
  print("caught ${e.message}"); // expect: caught from inside
}

// Nested try blocks catch at the innermost matching level.
try {
  try {
    throw "inner";
  } catch (e) {
    print("inner caught ${e.message}"); // expect: inner caught inner
    throw "rethrown";
  }
} catch (e) {
  print("outer caught ${e.message}"); // expect: outer caught rethrown
}

// A try block that does not throw runs its body and skips the catch.
try {
  print("no problem"); // expect: no problem
} catch (e) {
  print("never");
}

// Locals declared inside a try are discarded when it unwinds.
let total = 0;
for (let i = 0; i < 3; i = i + 1) {
  try {
    let scratch = i * 10;
    throw scratch;
  } catch (e) {
    total = total + e.payload;
  }
}
print(total); // expect: 30

print("done"); // expect: done
