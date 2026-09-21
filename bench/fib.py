# Same workload as fib.red, for comparison.
def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)


result = fib(32)
assert result == 2178309
print(result)
