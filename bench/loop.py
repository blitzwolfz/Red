# Same workload as loop.red, for comparison.
total = 0
for i in range(20000000):
    total = total + i % 7
print(total)
