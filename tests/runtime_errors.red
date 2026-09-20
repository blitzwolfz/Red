// An error with no handler stops the program and reports where it was.
fun inner() {
  return nil + 1;
}
fun outer() { return inner(); }
print("before");                     // expect: before
outer();
print("never reached");
// expect runtime error: Cannot add nil and number.
