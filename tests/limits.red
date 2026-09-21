// Leaving a try block by jumping, and the bounds on the value stack.

// break out of a try block has to close the handler it jumped out of.
// If it does not, the throw further down lands in the dead catch block
// above instead of the live one.
let log = [];
for (let i = 0; i < 3; i = i + 1) {
  try {
    if (i == 1) { break; }
    log.push(i);
  } catch (e) {
    log.push("wrong handler");
  }
}
try {
  throw "escaped";
} catch (e) {
  log.push(e.message);
}
print(log); // expect: [0, "escaped"]

// continue has the same problem and the same fix.
let seen = [];
for (let i = 0; i < 4; i = i + 1) {
  try {
    if (i % 2 == 0) { continue; }
    seen.push(i);
  } catch (e) {
    seen.push("wrong handler");
  }
}
try {
  throw "escaped again";
} catch (e) {
  seen.push(e.message);
}
print(seen); // expect: [1, 3, "escaped again"]

// Nested try blocks, leaving both at once.
let deep = [];
for (let i = 0; i < 2; i = i + 1) {
  try {
    try {
      if (i == 0) { break; }
    } catch (e) { deep.push("inner wrong"); }
  } catch (e) { deep.push("outer wrong"); }
}
try {
  throw "clean";
} catch (e) {
  deep.push(e.message);
}
print(deep); // expect: ["clean"]

// Running out of call frames is reported, not a crash.
fun recurse(n) {
  if (n <= 0) { return 0; }
  return 1 + recurse(n - 1);
}
try {
  recurse(100000);
  print("no overflow reported");
} catch (e) {
  print(e.message.contains("Stack overflow")); // expect: true
}

// The program keeps going after catching it.
print(recurse(50)); // expect: 50
