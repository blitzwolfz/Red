// Recursive fibonacci. Measures call overhead: frame setup, argument
// passing and return, with almost no allocation.
fun fib(n) {
  if (n < 2) { return n; }
  return fib(n - 1) + fib(n - 2);
}

const result = fib(32);
assert(result == 2178309, "fib(32) should be 2178309");
print(result);
