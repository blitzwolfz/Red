// Assertions, for tests written as Red programs.
//
//   import "std/testing" as t;
//
//   t.equal(add(2, 2), 4);
//   t.raises("io", fun () { read_file("nope"); });
//   t.report();
//
// `red test` compares a program's output against the `// expect:`
// comments in it, and needs nothing from this file. This is for the
// other shape of test: one that computes an answer and says whether it
// was right, and wants to say which of twenty checks failed.

let passed = 0;
let failed = 0;
let failures = [];
// Set by group(), and shown with anything that fails inside it.
let current = "";

fun group(name) {
  current = name;
  return name;
}

fun record(ok, description) {
  if (ok) {
    passed += 1;
    return true;
  }
  failed += 1;
  let where = description;
  if (current != "") { where = "${current}: ${description}"; }
  failures.push(where);
  eprint("FAIL ${where}");
  return false;
}

fun ok(value, description = "expected a true value") {
  return record(value == true, description);
}

fun not_ok(value, description = "expected a false value") {
  return record(value == false, description);
}

fun equal(actual, expected, description = nil) {
  let text = description;
  if (text == nil) {
    text = "expected ${repr(expected)}, got ${repr(actual)}";
  }
  return record(actual == expected, text);
}

fun not_equal(actual, expected, description = nil) {
  let text = description;
  if (text == nil) { text = "expected anything but ${repr(expected)}"; }
  return record(actual != expected, text);
}

fun near(actual, expected, tolerance = 0.000001, description = nil) {
  let text = description;
  if (text == nil) {
    text = "expected ${expected} within ${tolerance}, got ${actual}";
  }
  return record(abs(actual - expected) <= tolerance, text);
}

fun contains(haystack, needle, description = nil) {
  let text = description;
  if (text == nil) { text = "expected ${repr(haystack)} to contain ${repr(needle)}"; }
  return record(haystack.contains(needle), text);
}

fun is_nil(value, description = "expected nil") {
  return record(value == nil, description);
}

fun not_nil(value, description = "expected something other than nil") {
  return record(value != nil, description);
}

// Checks that `body` raises. With a kind, checks that too; that is
// usually the point, because "it failed somehow" is a weak thing to
// assert.
fun raises(kind, body, description = nil) {
  let text = description;
  if (text == nil) { text = "expected an error of kind ${repr(kind)}"; }
  try {
    body();
  } catch (e) {
    if (kind == nil) { return record(true, text); }
    return record(e.kind == kind,
                  "${text}, got ${repr(e.kind)}: ${e.message}");
  }
  return record(false, "${text}, but nothing was raised");
}

// Checks that `body` does not raise, and gives back what it returned.
fun succeeds(body, description = "expected no error") {
  try {
    const value = body();
    record(true, description);
    return value;
  } catch (e) {
    record(false, "${description}, got ${e.kind}: ${e.message}");
    return nil;
  }
}

fun fail(description) { return record(false, description); }

fun counts() { return {"passed": passed, "failed": failed}; }

fun reset() {
  passed = 0;
  failed = 0;
  failures = [];
  current = "";
}

// Prints the tally and exits non-zero when anything failed, so a test
// program can be run by anything that looks at exit codes.
fun report(name = "tests") {
  if (failed == 0) {
    print("${passed}/${passed} ${name} passed");
    return 0;
  }
  eprint("");
  for (let failure in failures) { eprint("  ${failure}"); }
  eprint("${failed} of ${passed + failed} ${name} failed");
  exit(1);
}
