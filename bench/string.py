# Same workload as string.red, for comparison.
parts = []
for i in range(500000):
    parts.append("item-%d-%d" % (i, i % 13))
joined = ",".join(parts)
hits = 0
for part in parts:
    if part.endswith("-0"):
        hits += 1
print(len(joined), hits)
