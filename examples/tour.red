// A tour of the language. This is the v1 example program from legacy/,
// rewritten in v2 syntax and extended with the features v1 did not have.
//
//   red examples/tour.red

// ---- bindings -------------------------------------------------------
// let can be reassigned. const cannot, and the compiler enforces it.
let counter = 0;
const greeting = "Hello";

// Type annotations are allowed anywhere a name is introduced. Nothing
// checks them. They are there for the reader.
let attempts: Int = 0;

// ---- strings --------------------------------------------------------
const who = "world";
print("${greeting}, ${who}!");
print("Two plus two is ${2 + 2}.");
print("upper: ${who.upper()}, length: ${who.len()}");

// ---- functions and closures ----------------------------------------
fun add(a: Int, b: Int) -> Int {
  return a + b;
}

fun makeCounter() {
  let count = 0;
  // The inner function captures count, so it survives after makeCounter
  // has returned.
  return fun () {
    count = count + 1;
    return count;
  };
}

const next = makeCounter();
next();
next();
print("closure count: ${next()}");
print("add(2, 3) = ${add(2, 3)}");

// ---- arrays and maps ------------------------------------------------
const animals = ["tiger", "otter", "crow"];
animals.push("bear");
print("animals: ${animals.join(", ")}");
print("sorted: ${animals.sort()}");
print("longest first: ${animals.sort(fun (a, b) { return a.len() > b.len(); })}");

const legs = {"tiger": 4, "crow": 2};
legs["otter"] = 4;
print("crow has ${legs["crow"]} legs");
print("known animals: ${legs.keys().sort()}");

// Higher order methods run a Red function for each element.
const lengths = animals.map(fun (name) { return name.len(); });
const total = lengths.reduce(fun (a, b) { return a + b; }, 0);
print("total name length: ${total}");

// ---- classes --------------------------------------------------------
class Animal {
  init(kind: String) {
    this.kind = kind;
    this.noises = 0;
  }

  describe() -> String {
    return "I am a ${this.kind}";
  }

  speak() -> String {
    this.noises = this.noises + 1;
    return "...";
  }
}

class Tiger < Animal {
  init() {
    super.init("tiger");
  }

  speak() -> String {
    // super reaches the method this one overrides.
    super.speak();
    return "ROAR";
  }

  describe() -> String {
    return super.describe() + ", and I have roared ${this.noises} times";
  }
}

const tiger = Tiger();
print(tiger.speak());
print(tiger.speak());
print(tiger.describe());

// ---- control flow ---------------------------------------------------
for (let i = 0; i < 6; i = i + 1) {
  if (i % 2 == 1) { continue; }
  if (i == 4) { break; }
  print("even: ${i}");
}

let countdown = 3;
while (countdown > 0) {
  print("t minus ${countdown}");
  countdown = countdown - 1;
}

// ---- errors ---------------------------------------------------------
fun divide(a, b) {
  if (b == 0) {
    throw error("cannot divide by zero", {"numerator": a});
  }
  return a / b;
}

try {
  divide(1, 0);
} catch (e) {
  print("caught: ${e.message} (numerator was ${e.payload["numerator"]})");
}

// Faults raised by the runtime are caught the same way.
try {
  const broken = [1, 2, 3];
  print(broken[99]);
} catch (e) {
  print("caught: ${e.message}");
}

// ---- tasks and channels ---------------------------------------------
fun square(n) { return n * n; }

const results = chan(8);
fun squareInto(n, out) { out.send(square(n)); }

let tasks = [];
for (let i = 1; i <= 4; i = i + 1) {
  tasks.push(spawn squareInto(i, results));
}
for (let i = 0; i < tasks.len(); i = i + 1) {
  tasks[i].join();
}
results.close();

let squares = [];
for (;;) {
  const value = results.recv();
  if (value == nil) { break; }
  squares.push(value);
}
print("squares: ${squares.sort()}");

// ---- the runtime itself ---------------------------------------------
print("platform: ${platform()}, cores: ${cpu_count()}");
const heap = gc_info();
print("collections so far: ${heap["collections"]}");
