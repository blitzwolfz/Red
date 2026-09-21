// The package manager.
//
// Red has no central registry. A dependency is named by the place it
// comes from, the same way Go names one: `github.com/owner/repo`. The
// name is both the import path and the address the code is fetched
// from, so there is no index anyone has to publish to and no account
// anyone has to hold.
//
// A project declares what it needs in `red.mod` beside its entry point.
// `red.lock` records the exact commit each requirement resolved to, so a
// second machine fetching the same manifest gets the same bytes. Fetched
// packages live in a shared cache under $RED_HOME (default ~/.red), one
// directory per module and version, and are never written to again.
//
// docs/packages.md is the user facing description.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace red {

// One `require` line.
struct Requirement {
  std::string path;     // github.com/owner/repo
  std::string version;  // v1.2.0, a branch, a commit, or "latest"
  bool direct = true;   // false when pulled in by another package
};

// One resolved dependency, as `red.lock` records it.
struct LockEntry {
  std::string path;
  std::string version;
  std::string commit;
  std::string remote;
};

// A parsed `red.mod`.
struct Manifest {
  // Where the file is, and the directory that holds it. The directory is
  // the project root: every import that begins with `modulePath` is
  // answered from inside it.
  std::string file;
  std::string root;

  std::string modulePath;   // this project's own import path
  std::string redVersion;   // the interpreter release it was written for
  std::vector<Requirement> requirements;
  // Import path -> a directory to use instead of the fetched copy. For
  // developing two packages side by side.
  std::map<std::string, std::string> replacements;
  // Import path -> the git remote to fetch from, when it cannot be
  // guessed from the path. For private hosts.
  std::map<std::string, std::string> sources;

  bool loaded = false;

  const Requirement* find(const std::string& path) const;
};

// Reads `red.mod` from `directory`, then from each directory above it,
// stopping at the first one that has it. Returns a manifest with
// `loaded` false when there is none, which is not an error: a script
// outside any project still runs.
Manifest loadManifest(const std::string& directory);

// Writes a manifest back out, keeping the canonical order of the
// sections rather than the order they were read in.
bool writeManifest(const Manifest& manifest, std::string* error);

// The lock file beside a manifest.
std::vector<LockEntry> loadLock(const std::string& root);
bool writeLock(const std::string& root, const std::vector<LockEntry>& entries,
               std::string* error);

// Root of the shared download cache: $RED_HOME/pkg, or ~/.red/pkg.
std::string packageCacheRoot();
// Where one module and version lives once it has been fetched.
std::string packageDirectory(const std::string& path,
                             const std::string& version);

// Turns an import path into the git remote it is fetched from, using the
// manifest's `source` lines first and the shape of the path otherwise.
std::string remoteFor(const Manifest& manifest, const std::string& path);

// Resolves an import that names a package rather than a file, for the VM.
// Returns the absolute path of the file to load, or an empty string when
// the request is not a package path or the package is not installed.
//
// `reason` is filled in when a package path was recognised but could not
// be answered, so the importer can say what to run rather than only that
// the file is missing.
std::string resolvePackageImport(const Manifest& manifest,
                                 const std::string& request,
                                 std::string* reason);

// True when a request looks like a package path rather than a file path:
// several segments, the first of which is a host name.
bool looksLikePackagePath(const std::string& request);

// The project the running program belongs to.
//
// Resolved once, from the directory of the entry point, before any code
// runs. Imports happen on task threads as well as the main one, so it is
// settled up front rather than looked up lazily from wherever the first
// import happens to occur.
void setProjectDirectory(const std::string& directory);
const Manifest& projectManifest();

// `red pkg ...`. Returns a process exit status.
int packageCommand(const std::vector<std::string>& args);

}  // namespace red
