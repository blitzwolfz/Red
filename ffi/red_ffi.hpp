// Red extension interface, for C++.
//
// A thin layer over red_ffi.h. The C header is the contract; this file
// adds nothing to it at run time and exists so that an extension written
// in C++ reads like C++ instead of like C with casts.
//
//   #include "red_ffi.hpp"
//
//   RED_FUNCTION(mathx_hypot) {
//     double a, b;
//     if (!args.number(0, &a) || !args.number(1, &b)) {
//       return ctx.fail("hypot() expects two numbers");
//     }
//     return red::ext::number(std::hypot(a, b));
//   }
//
// Red then reaches it with:
//
//   const lib = ffi_open("mathx.so");
//   const hypot = lib.sym("mathx_hypot");
//
// Everything here is header only and has no dependency beyond the C++
// standard library, so an extension needs no part of the interpreter's
// source to build. docs/libraries.md has the build commands.
#ifndef RED_FFI_HPP
#define RED_FFI_HPP

#include <string>
#include <string_view>

#include "red_ffi.h"

namespace red {
namespace ext {

// One Red value. Copyable, and convertible back to RedValue wherever the
// C interface wants one.
class Value {
 public:
  Value() : value_(red_nil()) {}
  Value(RedValue value) : value_(value) {}  // NOLINT: implicit on purpose
  operator RedValue() const { return value_; }

  bool is_nil() const { return red_is_nil(value_) != 0; }
  bool is_bool() const { return red_is_bool(value_) != 0; }
  bool is_number() const { return red_is_number(value_) != 0; }
  bool is_string() const { return red_is_string(value_) != 0; }

  bool as_bool() const { return red_as_bool(value_) != 0; }
  double as_number() const { return red_as_number(value_); }

  // Points into the interpreter's heap and stays valid until the next
  // collection, which can happen on any allocation. Copy it if it has to
  // outlive the call.
  std::string_view as_string() const {
    return std::string_view(red_string_chars(value_),
                            red_string_length(value_));
  }

 private:
  RedValue value_;
};

inline Value nil() { return Value(red_nil()); }
inline Value boolean(bool value) { return Value(red_bool(value ? 1 : 0)); }
inline Value number(double value) { return Value(red_number(value)); }

// The calling task. Anything that allocates or reports an error needs it.
class Context {
 public:
  explicit Context(void* raw) : raw_(raw) {}

  void* raw() const { return raw_; }

  Value string(std::string_view text) const {
    return Value(red_new_string(raw_, text.data(), text.size()));
  }

  // Raises an error in Red. Return the result straight away: the call is
  // abandoned.
  Value fail(const std::string& message) const {
    return Value(red_fail(raw_, message.c_str()));
  }

 private:
  void* raw_;
};

// The arguments of one call. Reading past the end gives nil rather than
// running off the array, because Red can call a symbol with any number of
// arguments.
class Args {
 public:
  Args(int count, RedValue* values) : count_(count), values_(values) {}

  int size() const { return count_; }

  Value operator[](int index) const {
    if (index < 0 || index >= count_) return nil();
    return Value(values_[index]);
  }

  // Reads argument `index` as a number. Gives false when it is missing or
  // is not a number, which is the usual shape of an argument check.
  bool number(int index, double* out) const {
    Value value = (*this)[index];
    if (!value.is_number()) return false;
    *out = value.as_number();
    return true;
  }

  bool string(int index, std::string_view* out) const {
    Value value = (*this)[index];
    if (!value.is_string()) return false;
    *out = value.as_string();
    return true;
  }

 private:
  int count_;
  RedValue* values_;
};

}  // namespace ext
}  // namespace red

// Defines a function Red can load with lib.sym("name"). The body receives
// `ctx` and `args` and returns a red::ext::Value.
#define RED_FUNCTION(name)                                              \
  static ::red::ext::Value name##_body(::red::ext::Context ctx,         \
                                       ::red::ext::Args args);          \
  extern "C" RedValue name(void* context, int argc, RedValue* argv) {   \
    return name##_body(::red::ext::Context(context),                    \
                       ::red::ext::Args(argc, argv));                   \
  }                                                                     \
  static ::red::ext::Value name##_body(::red::ext::Context ctx,         \
                                       ::red::ext::Args args)

#endif  // RED_FFI_HPP
