// Functions, recursion, closures and first class use.

fun add(a, b) { return a + b; }
print(add(2, 3));                    // expect: 5

// A function with no return answers nil.
fun noReturn() { }
print(noReturn());                   // expect: nil

fun fib(n) {
  if (n < 2) { return n; }
  return fib(n - 1) + fib(n - 2);
}
print(fib(15));                      // expect: 610

// Closures capture the variable, not its value at capture time.
fun counter() {
  let n = 0;
  return fun () { n = n + 1; return n; };
}
const next = counter();
next();
next();
print(next());                       // expect: 3

// Two closures over the same variable share it.
fun pair() {
  let n = 0;
  return [fun () { n = n + 1; }, fun () { return n; }];
}
const both = pair();
both[0]();
both[0]();
print(both[1]());                    // expect: 2

// Each call to the maker gets its own captured variable.
fun makeAdder(amount) {
  return fun (x) { return x + amount; };
}
const add10 = makeAdder(10);
const add100 = makeAdder(100);
print(add10(5), add100(5));          // expect: 15 105

// Closures nested more than one level deep reach through each level.
fun outer() {
  let a = "a";
  fun middle() {
    fun inner() { return a; }
    return inner();
  }
  return middle();
}
print(outer());                      // expect: a

// Functions are values.
const ops = {"add": add, "sub": fun (a, b) { return a - b; }};
print(ops["add"](8, 2), ops["sub"](8, 2));  // expect: 10 6

// Type annotations are allowed and ignored.
fun typed(a: Int, b: String) -> Bool { return true; }
print(typed(1, "x"));                // expect: true
