// A finally block runs on every way out of a try: falling off the end, a
// caught error, an error nothing matched, and return, break or continue.

let log = [];
fun note(text) { log.push(text); }

// Falling off the end.
try { note("body"); } finally { note("f1"); }
print(log.join(","));                    // expect: body,f1

// A caught error.
log = [];
try { throw "x"; } catch (e) { note("caught"); } finally { note("f2"); }
print(log.join(","));                    // expect: caught,f2

// An error nothing matched still leaves, but only after the finally.
log = [];
try {
  try {
    throw "y";
  } catch (e: "nope") {
    note("wrong");
  } finally {
    note("f3");
  }
} catch (e) {
  note("outer:${e.message}");
}
print(log.join(","));                    // expect: f3,outer:y

// A catch block that throws does not skip the finally.
log = [];
try {
  try {
    throw "z";
  } catch (e) {
    throw "from catch";
  } finally {
    note("f4");
  }
} catch (e) {
  note("outer:${e.message}");
}
print(log.join(","));                    // expect: f4,outer:from catch

// return, including from inside a catch.
log = [];
fun returning() {
  try { return "value"; } finally { note("f5"); }
}
note(returning());
fun returningFromCatch() {
  try { throw "q"; } catch (e) { return "caught value"; } finally { note("f6"); }
}
note(returningFromCatch());
print(log.join(","));                    // expect: f5,value,f6,caught value

// break and continue.
log = [];
for (let i in [1, 2, 3]) {
  try {
    if (i == 2) { break; }
    note("body${i}");
  } finally {
    note("f7-${i}");
  }
}
print(log.join(","));                    // expect: body1,f7-1,f7-2

log = [];
for (let i in [1, 2, 3]) {
  try {
    if (i == 2) { continue; }
    note("body${i}");
  } finally {
    note("f8-${i}");
  }
}
print(log.join(","));                    // expect: body1,f8-1,f8-2,body3,f8-3

// Nested try blocks run their finallys innermost first.
log = [];
fun nested() {
  try {
    try {
      return "deep";
    } finally {
      note("inner");
    }
  } finally {
    note("outer");
  }
}
note(nested());
print(log.join(","));                    // expect: inner,outer,deep

// A finally with no catch lets the error through.
log = [];
fun onlyFinally() {
  try { throw "boom"; } finally { note("f9"); }
}
try { onlyFinally(); } catch (e) { note("got:${e.message}"); }
print(log.join(","));                    // expect: f9,got:boom

// Values set in the try are visible in the finally.
let total = 0;
for (let i in range(0, 50)) {
  try {
    total += i;
    if (i % 5 == 0) { continue; }
  } finally {
    total += 1;
  }
}
print(total);                            // expect: 1275

// The stack stays clean over many passes.
let count = 0;
for (let i in range(0, 200)) {
  try {
    if (i % 2 == 0) { throw "even"; }
    count += 1;
  } catch (e) {
    count += 10;
  } finally {
    count += 100;
  }
}
print(count);                            // expect: 21100
