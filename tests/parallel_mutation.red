// Mutating one shared aggregate from several tasks remains coherent.
const values = [];

fun appendMany(shared, start) {
  for (let i = 0; i < 250; i = i + 1) {
    shared.push(start + i);
  }
}

const first = spawn appendMany(values, 0);
const second = spawn appendMany(values, 1000);
const third = spawn appendMany(values, 2000);
const fourth = spawn appendMany(values, 3000);
first.join();
second.join();
third.join();
fourth.join();

print(values.len()); // expect: 1000
print(values.contains(0)); // expect: true
print(values.contains(1249)); // expect: true
print(values.contains(2000)); // expect: true
print(values.contains(3249)); // expect: true
