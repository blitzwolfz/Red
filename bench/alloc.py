# Same workload as alloc.red, for comparison.
class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y

    def distance_squared(self):
        return self.x * self.x + self.y * self.y


far = 0
for _ in range(4000):
    points = [Point(i % 31, i % 17) for i in range(500)]
    for point in points:
        if point.distance_squared() > 500:
            far += 1
print(far)
