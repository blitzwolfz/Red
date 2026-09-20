# Same workload as method.red, for comparison.
class Base:
    def __init__(self):
        self.count = 0

    def step(self):
        self.count = self.count + 1
        return self.count


class Middle(Base):
    def step(self):
        return super().step()


class Leaf(Middle):
    def step(self):
        return super().step()


leaf = Leaf()
for _ in range(5000000):
    leaf.step()
print(leaf.count)
