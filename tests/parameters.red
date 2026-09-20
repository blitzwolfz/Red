// Default values and rest parameters.

fun greet(name, greeting = "Hello", punctuation = "!") {
  return "${greeting}, ${name}${punctuation}";
}
print(greet("ann"));                       // expect: Hello, ann!
print(greet("bob", "Hi"));                 // expect: Hi, bob!
print(greet("cal", "Hey", "?"));           // expect: Hey, cal?

// An explicit nil is not the same as leaving the argument out.
fun show(value = "missing") { return str(value); }
print(show());                             // expect: missing
print(show(nil));                          // expect: nil

// A default can refer to parameters declared before it.
fun box(width, height = width) {
  return "${width}x${height}";
}
print(box(3));                             // expect: 3x3
print(box(3, 4));                          // expect: 3x4

// Defaults are evaluated on each call, not shared between calls.
fun collect(item, into = []) {
  into.push(item);
  return into;
}
print(collect(1));                         // expect: [1]
print(collect(2));                         // expect: [2]

// Rest parameters gather whatever is left.
fun total(...numbers) {
  let sum = 0;
  for (let n in numbers) { sum += n; }
  return sum;
}
print(total());                            // expect: 0
print(total(1, 2, 3));                     // expect: 6

fun label(prefix, ...rest) {
  return "${prefix}: ${rest.join(",")} (${rest.len()})";
}
print(label("nums"));                      // expect: nums:  (0)
print(label("nums", 1, 2));                // expect: nums: 1,2 (2)

// Rest works alongside defaults.
fun mixed(a, b = 10, ...rest) {
  return "${a} ${b} ${rest}";
}
print(mixed(1));                           // expect: 1 10 []
print(mixed(1, 2));                        // expect: 1 2 []
print(mixed(1, 2, 3, 4));                  // expect: 1 2 [3, 4]

// Methods take defaults and rest too.
class Greeter {
  init(name, greeting = "Hello") {
    this.name = name;
    this.greeting = greeting;
  }
  say(...extras) {
    return "${this.greeting}, ${this.name}${extras.join("")}";
  }
}
print(Greeter("ann").say());               // expect: Hello, ann
print(Greeter("bob", "Yo").say("!", "!")); // expect: Yo, bob!!

// Too few arguments is still an error.
fun needsTwo(a, b) { return a + b; }
try {
  needsTwo(1);
} catch (e) {
  print(e.message);                        // expect: Expected 2 arguments but got 1.
}

// So is too many.
try {
  greet("a", "b", "c", "d");
} catch (e) {
  print(e.message);                        // expect: Expected between 1 and 3 arguments but got 4.
}

// A rest function reports its minimum.
try {
  label();
} catch (e) {
  print(e.message);                        // expect: Expected at least 1 argument but got 0.
}
