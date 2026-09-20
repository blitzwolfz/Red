// Importing, naming and sharing modules.

import "modules/math_util.red";      // expect: math_util loaded
import "modules/counter.red" as c;

print(math_util.square(7));          // expect: 49
print(math_util.PI);                 // expect: 3.14159

const point = math_util.Point(3, 4);
print(point.length());               // expect: 5

// A second import of the same file reuses the loaded module rather than
// running it again, so "math_util loaded" is not printed twice.
import "modules/math_util.red" as again;
print(again.square(3));              // expect: 9

// State in a module is shared by every importer.
c.bump();
c.bump();
print(c.bump());                     // expect: 3

// Reading a name a module does not define is an error.
try {
  print(math_util.missing);
} catch (e) {
  print(e.message);                  // expect: Undefined name 'missing' in module.
}
