// Classes, methods, fields, inheritance and super.

class Animal {
  init(kind) {
    this.kind = kind;
    this.legs = 4;
  }
  describe() { return "a ${this.kind} with ${this.legs} legs"; }
  speak() { return "..."; }
}

const cat = Animal("cat");
print(cat.describe()); // expect: a cat with 4 legs
print(cat.speak());    // expect: ...
print(cat.kind);       // expect: cat

// Fields can be added after construction.
cat.name = "Mittens";
print(cat.name); // expect: Mittens

class Dog < Animal {
  init(name) {
    super.init("dog");
    this.name = name;
  }
  speak() { return "woof"; }
  describe() { return super.describe() + ", called ${this.name}"; }
}

const dog = Dog("Rex");
print(dog.speak());    // expect: woof
print(dog.describe()); // expect: a dog with 4 legs, called Rex

// A subclass is still usable wherever the parent was.
const animals = [cat, dog];
for (let i = 0; i < animals.len(); i = i + 1) {
  print(animals[i].speak());
}
// expect: ...
// expect: woof

// Methods are values, and keep their receiver when passed around.
const speak = dog.speak;
print(speak()); // expect: woof

// Three levels of inheritance.
class Puppy < Dog {
  speak() { return super.speak() + " (small)"; }
}
print(Puppy("Bit").speak()); // expect: woof (small)

// A field holding a function shadows a method with the same name.
class Shadow {
  init() { this.run = fun () { return "field"; }; }
  run() { return "method"; }
}
print(Shadow().run()); // expect: field
