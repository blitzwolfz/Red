// Imported by modules.red. Not run on its own.
const PI = 3.14159;

fun square(n) { return n * n; }

fun area(radius) { return PI * square(radius); }

class Point {
  init(x, y) {
    this.x = x;
    this.y = y;
  }
  length() { return sqrt(square(this.x) + square(this.y)); }
}

// Module level code runs once, the first time the module is imported.
print("math_util loaded");
