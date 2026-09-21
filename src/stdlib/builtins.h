// Registration points for everything the runtime provides to Red code.
#pragma once

#include <string>
#include <vector>

#include "../object.h"
#include "../runtime.h"

namespace red {

class VM;

// Installs the free functions that every module can see.
void installBuiltins(Runtime& runtime);

// Looks up a method provided by the runtime for a built-in type, for
// example `arr.push` or `ch.send`. Returns nullptr when the type has no
// method with that name.
ObjNative* lookupBuiltinMethod(Runtime& runtime, Value receiver,
                               ObjString* name);

// Helpers used by the individual standard library files.
void defineGlobalFn(Runtime& runtime, const char* name, NativeFn fn,
                    int arity);
// A builtin that is a plain value rather than a function, such as PI. A
// module may still declare its own binding with the same name, which
// takes priority.
void defineGlobalValue(Runtime& runtime, const char* name, Value value);
void defineMethodFn(Runtime& runtime, ObjType type, const char* name,
                    NativeFn fn, int arity);

// Per area registration. Each is called once at startup.
void installCore(Runtime& runtime);
void installIO(Runtime& runtime);
void installOS(Runtime& runtime);
void installNet(Runtime& runtime);
void installConcurrency(Runtime& runtime);
void installFFI(Runtime& runtime);
void installLegacy(Runtime& runtime);

// Absolute path of the bundled legacy jar, or an empty string when it is
// not installed.
std::string findLegacyJar();
// Shell command that runs a script on the v1 Java interpreter. Empty when
// the jar cannot be found.
std::string legacyCommand(const std::string& scriptPath, bool captureOutput);

// Path of the running interpreter, used to find the bundled legacy jar
// and the library directory that ships beside it.
void setExecutablePath(const std::string& path);
const std::string& executablePath();

// Directories searched for a library that is not next to the file asking
// for it: every entry of RED_PATH, then lib/red and lib beside the
// interpreter's own directory. Used by `import` and by ffi_open().
// docs/libraries.md describes the order.
std::vector<std::string> librarySearchPaths();

// Finds `request` on the library search path. Returns an empty string
// when nothing matches. An absolute request is returned unchanged if it
// exists.
std::string findOnLibraryPath(const std::string& request);

}  // namespace red
