// A try block can have several catch clauses. The first one whose filter
// matches runs, and an error nothing matches carries on outwards.

// Every error carries a kind. The runtime uses a fixed set of names.
try { let x = 1 / 0; } catch (e) { print(e.kind); }             // expect: zero-division
try { undefinedThing(); } catch (e) { print(e.kind); }          // expect: name
try { nil +1; } catch (e) { print(e.kind); }                    // expect: type
try { [1][9]; } catch (e) { print(e.kind); }                    // expect: index
try { assert(false); } catch (e) { print(e.kind); }             // expect: assert
try { open("no_such_file", "r"); } catch (e) { print(e.kind); } // expect: io
try { throw "plain"; } catch (e) { print(e.kind); }             // expect: user

// A string filter selects on the kind.
fun classify(mode) {
  try {
    switch (mode) {
      case 0: let x = 1 / 0;
      case 1: undefinedThing();
      case 2: nil +1;
      default: throw "something else";
    }
  } catch (e: "zero-division") {
    return "maths";
  } catch (e: "name") {
    return "name";
  } catch (e: "type") {
    return "type";
  } catch (e) {
    return "other (${e.kind})";
  }
}
print(classify(0), classify(1), classify(2), classify(3));
// expect: maths name type other (user)

// Throwing a class instance gives the error that class's name as its
// kind, and a class filter matches it.
class AppError {
  init(message) { this.message = message; }
}
class DiskError < AppError { }
class NetworkError < AppError { }

try {
  throw DiskError("disk gone");
} catch (e: DiskError) {
  print("${e.kind}: ${e.message}"); // expect: DiskError: disk gone
}

// A filter matches subclasses, so a parent catches its children.
fun route(error) {
  try {
    throw error;
  } catch (e: NetworkError) {
    return "network";
  } catch (e: AppError) {
    return "app";
  } catch (e) {
    return "unknown";
  }
}
print(route(NetworkError("a"))); // expect: network
print(route(DiskError("b")));    // expect: app
print(route(AppError("c")));     // expect: app
print(route("not a class"));     // expect: unknown

// The thrown instance stays reachable as the payload.
try {
  throw DiskError("details here");
} catch (e: AppError) {
  print(e.payload.message); // expect: details here
}

// An error that nothing matches carries on to the enclosing try, with
// its message and kind unchanged.
fun inner() {
  try {
    throw error("from inner", nil, "special");
  } catch (e: "zero-division") {
    return "wrong";
  }
}
try {
  inner();
} catch (e: "special") {
  print("outer got ${e.message}"); // expect: outer got from inner
}

// An unmatched error leaves a function the same way an uncaught one
// would, so a caller still sees it.
fun neverMatches() {
  try {
    throw "escaping";
  } catch (e: "nope") {
    return "wrong";
  }
  return "also wrong";
}
try {
  neverMatches();
} catch (e) {
  print(e.message); // expect: escaping
}

// error() takes an explicit kind as its third argument.
try {
  throw error("custom failure", {"code": 7}, "my-kind");
} catch (e: "my-kind") {
  print(e.kind, e.message, e.payload["code"]); // expect: my-kind custom failure 7
}

// Clauses are tried in order, so a broad one placed first wins.
try {
  throw DiskError("x");
} catch (e: AppError) {
  print("broad first"); // expect: broad first
} catch (e: DiskError) {
  print("never reached");
}

// Running this many times must leave the stack clean.
let counts = {"a": 0, "b": 0, "other": 0};
for (let i in range(0, 300)) {
  try {
    switch (i % 3) {
      case 0: throw error("a", nil, "kindA");
      case 1: throw error("b", nil, "kindB");
      default: throw "c";
    }
  } catch (e: "kindA") {
    counts["a"] += 1;
  } catch (e: "kindB") {
    counts["b"] += 1;
  } catch (e) {
    counts["other"] += 1;
  }
}
print(counts["a"], counts["b"], counts["other"]); // expect: 100 100 100

// A filter that is neither a string nor a class is reported.
try {
  try {
    throw "x";
  } catch (e: 42) {
    print("never");
  }
} catch (e: "type") {
  print(e.message); // expect: A catch filter must be a string or a class, got number.
}
print("done"); // expect: done
