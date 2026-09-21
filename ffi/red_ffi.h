/*
 * Red extension interface.
 *
 * An extension is a shared library. Each function it exports for Red has
 * the signature RedForeignFn below. Red code loads the library with
 * ffi_open() and picks out functions with lib.sym("name").
 *
 * The helper functions are defined in the red binary itself. On macOS,
 * link the extension with -undefined dynamic_lookup so they resolve when
 * the library is loaded. On Linux no extra flag is needed.
 */
#ifndef RED_FFI_H
#define RED_FFI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Mirrors the tagged value used inside the interpreter. Do not build one
 * of these by hand: use the constructors below, which keep the tag and
 * the payload in step.
 */
typedef struct {
  unsigned char type;
  union {
    int boolean;
    double number;
    void* obj;
  } as;
} RedValue;

/*
 * context identifies the calling task. Pass it back to any helper that
 * takes one. argv points at the arguments and stays valid for the length
 * of the call.
 */
typedef RedValue (*RedForeignFn)(void* context, int argc, RedValue* argv);

RedValue red_nil(void);
RedValue red_bool(int value);
RedValue red_number(double value);

int red_is_nil(RedValue value);
int red_is_bool(RedValue value);
int red_is_number(RedValue value);
int red_is_string(RedValue value);

int red_as_bool(RedValue value);
double red_as_number(RedValue value);

/* Valid until the next garbage collection. Copy it if you keep it. */
const char* red_string_chars(RedValue value);
size_t red_string_length(RedValue value);

/* Allocates a Red string. Needs the context because it touches the heap. */
RedValue red_new_string(void* context, const char* chars, size_t length);

/*
 * Reports an error to the interpreter. Return its result immediately: the
 * call is abandoned and the error is raised in Red.
 */
RedValue red_fail(void* context, const char* message);

#ifdef __cplusplus
}
#endif

#endif /* RED_FFI_H */
