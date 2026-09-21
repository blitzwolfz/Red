// Method dispatch through an inheritance chain. Measures property lookup
// and the fused invoke instruction.
class Base {
  init() { this.count = 0; }
  step() { this.count = this.count + 1; return this.count; }
}
class Middle < Base {
  step() { return super.step(); }
}
class Leaf < Middle {
  step() { return super.step(); }
}

const leaf = Leaf();
for (let i = 0; i < 5000000; i = i + 1) {
  leaf.step();
}
print(leaf.count);
