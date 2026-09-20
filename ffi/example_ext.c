/*
 * Example Red extension.
 *
 * Build it as part of the normal CMake build, then use it from Red:
 *
 *   const lib = ffi_open("./example_ext.so");
 *   const hypot = lib.sym("ext_hypot");
 *   print(hypot(3, 4));
 */
#include <math.h>
#include <string.h>

#include "red_ffi.h"

/* Returns the length of the hypotenuse of a right triangle. */
RedValue ext_hypot(void* context, int argc, RedValue* argv) {
  if (argc != 2 || !red_is_number(argv[0]) || !red_is_number(argv[1])) {
    return red_fail(context, "ext_hypot expects two numbers");
  }
  double a = red_as_number(argv[0]);
  double b = red_as_number(argv[1]);
  return red_number(sqrt(a * a + b * b));
}

/* Reverses a string, to show how to allocate one. */
RedValue ext_reverse(void* context, int argc, RedValue* argv) {
  if (argc != 1 || !red_is_string(argv[0])) {
    return red_fail(context, "ext_reverse expects one string");
  }
  const char* chars = red_string_chars(argv[0]);
  size_t length = red_string_length(argv[0]);

  char stackBuffer[256];
  char* buffer = stackBuffer;
  if (length >= sizeof(stackBuffer)) {
    /* Long strings would not fit, so report instead of overflowing. */
    return red_fail(context, "ext_reverse only handles short strings");
  }
  for (size_t i = 0; i < length; i++) buffer[i] = chars[length - 1 - i];

  return red_new_string(context, buffer, length);
}

/* Adds up every argument, to show a variadic extension. */
RedValue ext_sum(void* context, int argc, RedValue* argv) {
  double total = 0;
  for (int i = 0; i < argc; i++) {
    if (!red_is_number(argv[i])) {
      return red_fail(context, "ext_sum expects numbers");
    }
    total += red_as_number(argv[i]);
  }
  return red_number(total);
}
