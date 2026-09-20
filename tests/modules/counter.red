// Holds state, to show that a module is loaded once and then shared.
let count = 0;

fun bump() {
  count = count + 1;
  return count;
}
