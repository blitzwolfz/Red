// Enums are named constants. Each member is a single object, so members
// compare by identity and can be used as map keys.

enum Colour { Red, Green, Blue }

print(Colour.Red);                     // expect: Colour.Red
print(Colour.Green.name);              // expect: Green
print(Colour.Green.value);             // expect: 1
print(Colour.Blue.value);              // expect: 2
print(type(Colour), type(Colour.Red)); // expect: enum enum member

// Values count up from zero unless a member says otherwise, and counting
// carries on from the last one given.
enum Op { Add = 10, Sub, Mul = 20, Div }
print(Op.Add.value, Op.Sub.value, Op.Mul.value, Op.Div.value);
// expect: 10 11 20 21

// Negative values are allowed.
enum Level { Low = -1, Mid, High }
print(Level.Low.value, Level.Mid.value, Level.High.value);
// expect: -1 0 1

// The enum itself answers questions about its members.
print(Colour.values());            // expect: [Colour.Red, Colour.Green, Colour.Blue]
print(Colour.len(), len(Colour));  // expect: 3 3
print(Colour.name());              // expect: Colour
print(Op.from(20));                // expect: Op.Mul
print(Op.from(99));                // expect: nil
print(Colour.Red.owner == Colour); // expect: true

// Identity, which is what makes them cheap to compare.
print(Colour.Red == Colour.Red);   // expect: true
print(Colour.Red == Colour.Green); // expect: false
print(Colour.Red == 0);            // expect: false

// They work as map keys and in switch.
const warmth = {};
warmth[Colour.Red] = "warm";
warmth[Colour.Blue] = "cool";
print(warmth[Colour.Red], warmth[Colour.Green]); // expect: warm nil

fun describe(colour) {
  switch (colour) {
    case Colour.Red, Colour.Green: return "primary-ish";
    case Colour.Blue: return "cool";
    default: return "unknown";
  }
}
print(describe(Colour.Red), describe(Colour.Blue)); // expect: primary-ish cool

// Their value indexes an array, which is how a table keyed by an enum
// is written.
const names = ["red", "green", "blue"];
print(names[Colour.Green.value]); // expect: green

// for-in walks the members.
let collected = [];
for (let member in Colour.values()) { collected.push(member.name); }
print(collected); // expect: ["Red", "Green", "Blue"]

// An enum declared inside a block is scoped to it.
{
  enum Inner { One }
  print(Inner.One); // expect: Inner.One
}

// The binding is const.
print(Colour.Red); // expect: Colour.Red

// Asking for a member that does not exist is an error, not nil, because
// it is almost always a typo.
try {
  print(Colour.Purple);
} catch (e: "name") {
  print(e.message); // expect: Enum Colour has no member 'Purple'.
}
