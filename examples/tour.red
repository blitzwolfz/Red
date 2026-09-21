// A tour of the language. This is the v1 example program from legacy/,
// rewritten in v2 syntax and extended with the features v1 did not have.
//
//   red examples/tour.red

// ---- bindings -------------------------------------------------------
// let can be reassigned. const cannot, and the compiler enforces it.
let counter = 0;
const greeting = "Hello";

// Compound assignment works on variables, fields and elements.
counter += 5;
counter *= 2;

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
print("counter is ${counter}");

// A parameter can have a default, and the last one can gather the rest.
fun join(separator = ", ", ...parts) {
  return parts.join(separator);
}
print("empty join gives '${join()}'");
print(join(" and ", "salt", "pepper"));

// An omitted argument and an explicit nil are different things.
fun report(value = "nothing given") { return str(value); }
print("${report()} / ${report(nil)}");

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

// for-in walks an array, a map's keys, or a string's characters.
let loudest = "";
for (let name in animals) {
  if (name.len() > loudest.len()) { loudest = name; }
}
print("longest name: ${loudest}");

let legCount = 0;
for (let name in legs) { legCount += legs[name]; }
print("legs in total: ${legCount}");

// A pattern in the loop takes a key and a value at once.
let described = [];
for (let [name, count] in legs.entries()) {
  described.push("${name} has ${count}");
}
print(described.sort().join("; "));

// ---- enums ----------------------------------------------------------
// An enum is a set of named constants. Members print with their name,
// compare by identity, and can be map keys or switch cases.
enum Size { Small, Medium, Large }
enum Status { Ok = 200, NotFound = 404 }

print("sizes: ${Size.values()}, Large is ${Size.Large.value}");
print("404 is ${Status.from(404)}");

const description = {};
description[Size.Small] = "fits in a hand";
description[Size.Large] = "needs both arms";
print("Small: ${description[Size.Small]}");

// ---- destructuring --------------------------------------------------
// A pattern binds several names at once. It works on arrays, maps and
// instances, it nests, and the last part can gather the rest.
const [width, height] = [1920, 1080];
print("screen ${width}x${height}");

const [best, ...others] = animals.sort();
print("best ${best}, others ${others}");

const {kind: firstKind} = {"kind": "otter", "legs": 4};
print("read a field by name: ${firstKind}");

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
// switch compares a value against each case, and never falls through.
fun sound(kind) {
  switch (kind) {
    case "tiger", "lion": return "ROAR";
    case "otter": return "squeak";
    default: return "...";
  }
}
print("tiger says ${sound("tiger")}, otter says ${sound("otter")}");

// The counting for loop is still there when an index is what you want.
for (let i = 0; i < 6; i = i + 1) {
  if (i % 2 == 1) { continue; }
  if (i == 4) { break; }
  print("even: ${i}");
}

let countdown = 3;
while (countdown > 0) {
  print("t minus ${countdown}");
  countdown -= 1;
}

// ---- bits and bytes -------------------------------------------------
// Bitwise operators work on 32 bit integers. They bind tighter than
// comparison, so `a & b == c` means `(a & b) == c`.
const packed = (212 << 8) | 49;
print("packed ${packed}, high ${packed >> 8 & 255}, low ${packed & 255}");

// chr and code_at turn numbers into characters and back, which is what
// writing binary output needs.
print("chr(82) is ${chr(82)}, 'R' is ${"R".code_at(0)}");
print("bytes of 'Red': ${"Red".bytes()}");

// ---- errors ---------------------------------------------------------
// Errors are values. Every one carries a message, a kind and a trace.
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

// A try block can have several catch clauses. A string filter selects on
// the kind, and a class filter matches a thrown instance, including
// subclasses. An error nothing matches carries on outwards.
class ConfigError {
  init(message) { this.message = message; }
}

fun load(source) {
  try {
    switch (source) {
      case "missing": throw ConfigError("no such setting");
      case "maths": return 1 / 0;
      default: return source[99];
    }
  } catch (e: ConfigError) {
    return "config problem: ${e.message}";
  } catch (e: "zero-division") {
    return "arithmetic problem";
  } catch (e) {
    return "${e.kind} problem: ${e.message}";
  }
}
print(load("missing"));
print(load("maths"));
print(load("abc"));

// ---- tasks and channels ---------------------------------------------
fun square(n) { return n * n; }

const results = chan(8);
fun squareInto(n, out) { out.send(square(n)); }

let tasks = [];
for (let i = 1; i <= 4; i = i + 1) {
  tasks.push(spawn squareInto(i, results));
}
for (let task in tasks) { task.join(); }
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
