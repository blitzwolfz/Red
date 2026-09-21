// Bundling a program and the interpreter into one executable.
//
// `red build app.red -o app` copies the running interpreter, appends the
// compiled program to the copy, and writes a trailer on the end saying
// how long the program is. Running the copy finds the trailer, skips the
// compiler and the file system entirely, and runs what is inside it.
//
// The payload goes after everything the loader cares about, so the file
// is still a valid executable: a Mach-O's code signature covers the
// bytes up to its own end, and an ELF is described by headers that point
// backwards from the top. Neither format minds what follows.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace red {

// The last bytes of a bundled executable: the payload length, then this.
// A plain interpreter does not have it, which is how the two are told
// apart.
constexpr char kBundleMagic[8] = {'R', 'E', 'D', 'B', 'U', 'N', 'D', 'L'};
constexpr size_t kBundleTrailerSize = 8 + sizeof(kBundleMagic);

// One module inside a bundle: its compiled code, and where each of its
// imports leads.
//
// The paths are the ones the build machine resolved, which the machine
// running the program will not have. They are only ever used as keys, so
// that does not matter; what makes an import work is `links`, which
// answers "this module asked for that spelling" without going near a
// file system.
struct BundledModule {
  std::string path;
  std::string name;
  // The .redc bytes, exactly what `red compile` would have written.
  std::string code;
  // Import spelling as written, paired with the path of the module it
  // resolved to at build time.
  std::vector<std::pair<std::string, std::string>> links;
};

struct Bundle {
  std::vector<BundledModule> modules;
  // Index into `modules` of the one to run.
  size_t entry = 0;

  // The module stored under this path, or nullptr.
  const BundledModule* find(const std::string& path) const;
  // Where `request`, written inside the module at `from`, leads. Empty
  // when that module is not in the bundle or did not import that.
  std::string resolve(const std::string& from,
                      const std::string& request) const;
};

// The archive that goes on the end of the executable.
std::string encodeBundle(const Bundle& bundle);
bool decodeBundle(const std::string& bytes, Bundle* out, std::string* reason);

// Reads the program out of an executable. Returns false when there is no
// trailer, which is the ordinary case for the interpreter itself.
bool readBundle(const std::string& executable, std::string* payload);

// Writes `payload` into a copy of `executable` at `outPath`, marked
// executable. When `executable` is itself a bundle its payload is
// dropped, so building from a bundled program does not nest them.
// Returns false and fills `reason` on failure.
bool writeBundle(const std::string& executable, const std::string& payload,
                 const std::string& outPath, std::string* reason);

// The path of the running executable, asked of the operating system
// rather than taken from argv[0]. A bundled program is usually started
// through the PATH or a symlink, where argv[0] is not a path at all.
// Falls back to `fallback` when the system cannot say.
std::string selfExecutablePath(const std::string& fallback);

}  // namespace red
