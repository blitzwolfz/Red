#include "pkg.h"

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

#include "common.h"
#include "util.h"

namespace red {

namespace {

// ---------------------------------------------------------------------
// Small text helpers. The manifest is a line format on purpose: it is
// read by people as often as by the tool, and a line format diffs well.

std::string trim(const std::string& text) {
  size_t start = 0;
  size_t end = text.size();
  while (start < end && std::isspace((unsigned char)text[start])) start++;
  while (end > start && std::isspace((unsigned char)text[end - 1])) end--;
  return text.substr(start, end - start);
}

std::vector<std::string> fields(const std::string& line) {
  std::vector<std::string> out;
  std::istringstream stream(line);
  std::string word;
  while (stream >> word) out.push_back(word);
  return out;
}

bool startsWith(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() &&
         text.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& text, const std::string& suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> splitOn(const std::string& text, char separator) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find(separator, start);
    if (end == std::string::npos) end = text.size();
    parts.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return parts;
}

// mkdir -p. Returns false only when a component exists and is not a
// directory, or cannot be made.
bool makeDirectories(const std::string& path) {
  if (path.empty() || isDirectory(path)) return true;
  std::string parent = directoryOf(path);
  if (parent != path && !parent.empty() && parent != "/" && parent != ".") {
    if (!makeDirectories(parent)) return false;
  }
  if (::mkdir(path.c_str(), 0755) == 0) return true;
  return isDirectory(path);
}

std::string homeDirectory() {
  const char* home = std::getenv("HOME");
  return home != nullptr ? std::string(home) : std::string(".");
}

// ---------------------------------------------------------------------
// Running git.
//
// Everything the package manager fetches goes through the git command
// line. That is deliberate: it means a user's existing credential
// helpers, SSH keys, proxies and host rewrites all apply without this
// tool knowing anything about them.

struct CommandResult {
  int status = -1;
  std::string output;
};

// Quotes an argument for /bin/sh. Import paths come out of a manifest
// that may not be trusted, so nothing is ever pasted into a command line
// unquoted.
std::string shellQuote(const std::string& text) {
  std::string quoted = "'";
  for (char c : text) {
    if (c == '\'') {
      quoted += "'\\''";
    } else {
      quoted += c;
    }
  }
  quoted += "'";
  return quoted;
}

CommandResult run(const std::vector<std::string>& argv, bool captureStderr) {
  std::string command;
  for (size_t i = 0; i < argv.size(); i++) {
    if (i > 0) command += " ";
    command += shellQuote(argv[i]);
  }
  command += captureStderr ? " 2>&1" : " 2>/dev/null";

  CommandResult result;
  FILE* pipe = ::popen(command.c_str(), "r");
  if (pipe == nullptr) return result;
  char buffer[4096];
  while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    result.output += buffer;
  }
  int status = ::pclose(pipe);
  result.status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

bool haveGit() {
  static int cached = -1;
  if (cached < 0) cached = run({"git", "--version"}, false).status == 0 ? 1 : 0;
  return cached == 1;
}

// ---------------------------------------------------------------------
// Versions.
//
// Tags are compared as semantic versions when they look like one, so
// `latest` picks v1.10.0 over v1.9.0. Anything that does not parse sorts
// before everything that does, and ties fall back to a string compare.

struct SemVer {
  bool valid = false;
  long major = 0;
  long minor = 0;
  long patch = 0;
  std::string prerelease;
};

SemVer parseSemVer(const std::string& tag) {
  SemVer version;
  std::string text = tag;
  if (!text.empty() && (text[0] == 'v' || text[0] == 'V')) text = text.substr(1);

  size_t dash = text.find_first_of("-+");
  if (dash != std::string::npos) {
    version.prerelease = text.substr(dash + 1);
    text = text.substr(0, dash);
  }
  std::vector<std::string> parts = splitOn(text, '.');
  if (parts.empty() || parts.size() > 3) return version;
  long* targets[3] = {&version.major, &version.minor, &version.patch};
  for (size_t i = 0; i < parts.size(); i++) {
    if (parts[i].empty()) return version;
    for (char c : parts[i]) {
      if (!std::isdigit((unsigned char)c)) return version;
    }
    *targets[i] = std::strtol(parts[i].c_str(), nullptr, 10);
  }
  version.valid = true;
  return version;
}

// Is `a` an earlier release than `b`? A prerelease sorts before the
// release it leads to, as semver says.
bool earlier(const std::string& a, const std::string& b) {
  SemVer left = parseSemVer(a);
  SemVer right = parseSemVer(b);
  if (left.valid != right.valid) return right.valid;
  if (!left.valid) return a < b;
  if (left.major != right.major) return left.major < right.major;
  if (left.minor != right.minor) return left.minor < right.minor;
  if (left.patch != right.patch) return left.patch < right.patch;
  if (left.prerelease.empty() != right.prerelease.empty()) {
    return !right.prerelease.empty() ? false : true;
  }
  return left.prerelease < right.prerelease;
}

// A version string for a commit that has no tag, in the shape Go uses:
// v0.0.0-<short commit>. It sorts before every real release and says
// plainly that it is not one.
std::string pseudoVersion(const std::string& commit) {
  return "v0.0.0-" + commit.substr(0, 12);
}

// A directory name that cannot collide and cannot escape the cache. An
// import path is mostly safe already, but `..` in one would be a way out
// of the cache, so every segment is checked.
bool safePathSegment(const std::string& segment) {
  if (segment.empty() || segment == "." || segment == "..") return false;
  for (char c : segment) {
    if (c == '/' || c == '\\' || c == '\0') return false;
  }
  return true;
}

bool safeImportPath(const std::string& path) {
  if (path.empty() || path.front() == '/' || path.front() == '.') return false;
  for (const std::string& segment : splitOn(path, '/')) {
    if (!safePathSegment(segment)) return false;
  }
  return true;
}

}  // namespace

// ---------------------------------------------------------------------
// Manifest

const Requirement* Manifest::find(const std::string& path) const {
  for (const Requirement& requirement : requirements) {
    if (requirement.path == path) return &requirement;
  }
  return nullptr;
}

Manifest loadManifest(const std::string& directory) {
  Manifest manifest;
  std::string current = absolutePath(directory.empty() ? "." : directory);
  std::string file;
  for (;;) {
    std::string candidate = joinPath(current, "red.mod");
    if (fileExists(candidate)) {
      file = candidate;
      break;
    }
    std::string parent = directoryOf(current);
    if (parent == current || parent.empty()) break;
    current = parent;
  }
  if (file.empty()) return manifest;

  std::string source;
  if (!readFile(file, &source)) return manifest;

  manifest.file = file;
  manifest.root = directoryOf(file);
  manifest.loaded = true;

  for (const std::string& rawLine : splitOn(source, '\n')) {
    std::string line = trim(rawLine);
    size_t comment = line.find("//");
    if (comment != std::string::npos) line = trim(line.substr(0, comment));
    if (line.empty() || line[0] == '#') continue;

    std::vector<std::string> parts = fields(line);
    const std::string& keyword = parts[0];

    if (keyword == "module" && parts.size() >= 2) {
      manifest.modulePath = parts[1];
    } else if (keyword == "red" && parts.size() >= 2) {
      manifest.redVersion = parts[1];
    } else if (keyword == "require" && parts.size() >= 2) {
      Requirement requirement;
      requirement.path = parts[1];
      requirement.version = parts.size() >= 3 ? parts[2] : "latest";
      // A requirement nothing in this project imports directly is marked
      // so `red pkg tidy` can tell the two apart.
      requirement.direct =
          !(parts.size() >= 4 && parts[3] == "indirect");
      manifest.requirements.push_back(requirement);
    } else if (keyword == "replace" && parts.size() >= 4 && parts[2] == "=>") {
      manifest.replacements[parts[1]] = parts[3];
    } else if (keyword == "source" && parts.size() >= 3) {
      manifest.sources[parts[1]] = parts[2];
    }
  }
  return manifest;
}

bool writeManifest(const Manifest& manifest, std::string* error) {
  std::string text;
  text += "module " + manifest.modulePath + "\n";
  if (!manifest.redVersion.empty()) text += "red " + manifest.redVersion + "\n";

  std::vector<Requirement> ordered = manifest.requirements;
  std::sort(ordered.begin(), ordered.end(),
            [](const Requirement& a, const Requirement& b) {
              return a.path < b.path;
            });
  if (!ordered.empty()) text += "\n";
  for (const Requirement& requirement : ordered) {
    text += "require " + requirement.path + " " + requirement.version;
    if (!requirement.direct) text += " indirect";
    text += "\n";
  }
  if (!manifest.sources.empty()) text += "\n";
  for (const auto& entry : manifest.sources) {
    text += "source " + entry.first + " " + entry.second + "\n";
  }
  if (!manifest.replacements.empty()) text += "\n";
  for (const auto& entry : manifest.replacements) {
    text += "replace " + entry.first + " => " + entry.second + "\n";
  }

  std::ofstream out(manifest.file, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (error != nullptr) *error = "cannot write " + manifest.file;
    return false;
  }
  out << text;
  return out.good();
}

std::vector<LockEntry> loadLock(const std::string& root) {
  std::vector<LockEntry> entries;
  std::string source;
  if (!readFile(joinPath(root, "red.lock"), &source)) return entries;
  for (const std::string& rawLine : splitOn(source, '\n')) {
    std::string line = trim(rawLine);
    if (line.empty() || line[0] == '#') continue;
    std::vector<std::string> parts = fields(line);
    if (parts.size() < 3) continue;
    LockEntry entry;
    entry.path = parts[0];
    entry.version = parts[1];
    entry.commit = parts[2];
    if (parts.size() >= 4) entry.remote = parts[3];
    entries.push_back(entry);
  }
  return entries;
}

bool writeLock(const std::string& root, const std::vector<LockEntry>& entries,
               std::string* error) {
  std::vector<LockEntry> ordered = entries;
  std::sort(ordered.begin(), ordered.end(),
            [](const LockEntry& a, const LockEntry& b) {
              if (a.path != b.path) return a.path < b.path;
              return a.version < b.version;
            });

  std::string text =
      "# Written by `red pkg`. Commit this file: it is what makes a\n"
      "# second machine fetch exactly the code this one was built against.\n";
  for (const LockEntry& entry : ordered) {
    text += entry.path + " " + entry.version + " " + entry.commit;
    if (!entry.remote.empty()) text += " " + entry.remote;
    text += "\n";
  }

  std::string path = joinPath(root, "red.lock");
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (error != nullptr) *error = "cannot write " + path;
    return false;
  }
  out << text;
  return out.good();
}

// ---------------------------------------------------------------------
// The cache

std::string packageCacheRoot() {
  const char* home = std::getenv("RED_HOME");
  if (home != nullptr && home[0] != '\0') return joinPath(home, "pkg");
  return joinPath(joinPath(homeDirectory(), ".red"), "pkg");
}

std::string packageDirectory(const std::string& path,
                             const std::string& version) {
  if (!safeImportPath(path)) return "";
  // `owner/repo@v1.2.0` under the host's directory. The version is part
  // of the leaf so that two projects needing two versions of the same
  // package share a cache without fighting over it.
  std::vector<std::string> segments = splitOn(path, '/');
  std::string directory = packageCacheRoot();
  for (size_t i = 0; i + 1 < segments.size(); i++) {
    directory = joinPath(directory, segments[i]);
  }
  std::string leaf = segments.back();
  if (!version.empty()) leaf += "@" + version;
  return joinPath(directory, leaf);
}

std::string remoteFor(const Manifest& manifest, const std::string& path) {
  auto override_ = manifest.sources.find(path);
  if (override_ != manifest.sources.end()) return override_->second;

  // A path is host/owner/repo, and anything past the third segment names
  // a directory inside the repository rather than a different one.
  std::vector<std::string> segments = splitOn(path, '/');
  if (segments.size() < 3) return "";
  std::string repository =
      segments[0] + "/" + segments[1] + "/" + segments[2];
  return "https://" + repository + ".git";
}

bool looksLikePackagePath(const std::string& request) {
  if (request.empty() || request.front() == '/' || request.front() == '.') {
    return false;
  }
  size_t slash = request.find('/');
  if (slash == std::string::npos) return false;
  // The first segment has to look like a host: a dot and no spaces. That
  // is what keeps `util/text.red` a plain file import.
  std::string host = request.substr(0, slash);
  return host.find('.') != std::string::npos && safeImportPath(request);
}

namespace {

// Given a directory a package path resolved to, the file to actually
// load. A package may be one file, or a directory whose entry point is
// named after it.
std::string entryFileIn(const std::string& directory,
                        const std::string& leafName) {
  const char* const candidates[] = {nullptr, "mod.red", "lib.red", "main.red"};
  // A cached package's directory is named `repo@version`, and the file
  // inside it is named after the repository alone.
  std::string stem = leafName.substr(0, leafName.find('@'));
  std::string named = stem + ".red";
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    std::string file =
        joinPath(directory, i == 0 ? named : std::string(candidates[i]));
    if (fileExists(file) && !isDirectory(file)) return file;
  }
  return "";
}

// Turns "<something>/a/b" into the file it names inside `base`, whether
// it was written with the .red suffix or without one.
std::string fileUnder(const std::string& base, const std::string& relative) {
  if (relative.empty()) {
    return entryFileIn(base, base.substr(base.find_last_of('/') + 1));
  }
  std::string direct = joinPath(base, relative);
  if (fileExists(direct) && !isDirectory(direct)) return direct;
  if (!endsWith(relative, ".red")) {
    std::string suffixed = direct + ".red";
    if (fileExists(suffixed)) return suffixed;
    if (isDirectory(direct)) {
      return entryFileIn(direct, relative.substr(relative.find_last_of('/') + 1));
    }
  }
  return "";
}

}  // namespace

std::string resolvePackageImport(const Manifest& manifest,
                                 const std::string& request,
                                 std::string* reason) {
  if (!looksLikePackagePath(request)) return "";

  // The project's own module path. `import "example.com/app/util"` from
  // inside example.com/app is the same file as a relative import, but it
  // keeps working when the file moves.
  if (manifest.loaded && !manifest.modulePath.empty() &&
      (request == manifest.modulePath ||
       startsWith(request, manifest.modulePath + "/"))) {
    std::string relative = request.size() == manifest.modulePath.size()
                               ? std::string()
                               : request.substr(manifest.modulePath.size() + 1);
    std::string file = fileUnder(manifest.root, relative);
    if (!file.empty()) return file;
    if (reason != nullptr) {
      *reason = "Cannot import '" + request +
                "': it is inside this project, but there is no such file.";
    }
    return "";
  }

  // The longest matching prefix wins, so replacing a repository also
  // replaces the packages inside it.
  std::string bestPrefix;
  std::string bestTarget;
  for (const auto& entry : manifest.replacements) {
    if (request == entry.first || startsWith(request, entry.first + "/")) {
      if (entry.first.size() > bestPrefix.size()) {
        bestPrefix = entry.first;
        bestTarget = entry.second;
      }
    }
  }
  if (!bestPrefix.empty()) {
    std::string base = absolutePath(joinPath(manifest.root, bestTarget));
    std::string relative = request.size() == bestPrefix.size()
                               ? std::string()
                               : request.substr(bestPrefix.size() + 1);
    std::string file = fileUnder(base, relative);
    if (!file.empty()) return file;
    if (reason != nullptr) {
      *reason = "Cannot import '" + request + "': it is replaced by '" +
                bestTarget + "', which has no such file.";
    }
    return "";
  }

  // A vendored copy, if the project has one. Checked before the cache so
  // that `red pkg vendor` makes a project build with no network at all.
  if (manifest.loaded) {
    std::string vendored =
        fileUnder(joinPath(manifest.root, "vendor"), request);
    if (!vendored.empty()) return vendored;
  }

  // Otherwise the download cache, at whichever version this project
  // pinned. Longest prefix again: the requirement names a repository and
  // the request may name a package inside it.
  std::string prefix;
  std::string version;
  for (const Requirement& requirement : manifest.requirements) {
    if ((request == requirement.path ||
         startsWith(request, requirement.path + "/")) &&
        requirement.path.size() > prefix.size()) {
      prefix = requirement.path;
      version = requirement.version;
    }
  }
  if (prefix.empty()) {
    if (reason != nullptr) {
      *reason = "Cannot import '" + request +
                "': red.mod does not require it. Run `red pkg add " + request +
                "`.";
    }
    return "";
  }

  // The lock file, not the manifest, decides which bytes are used. A
  // requirement of `latest` or of a branch means something different
  // from one day to the next; the commit it resolved to does not.
  std::string resolved = version;
  for (const LockEntry& entry : loadLock(manifest.root)) {
    if (entry.path == prefix) {
      resolved = entry.version;
      break;
    }
  }

  std::string base = packageDirectory(prefix, resolved);
  if (base.empty() || !isDirectory(base)) {
    if (reason != nullptr) {
      *reason = "Cannot import '" + request + "': " + prefix + " " +
                resolved + " is not downloaded. Run `red pkg get`.";
    }
    return "";
  }
  std::string relative =
      request.size() == prefix.size() ? std::string()
                                      : request.substr(prefix.size() + 1);
  std::string file = fileUnder(base, relative);
  if (file.empty() && reason != nullptr) {
    *reason = "Cannot import '" + request + "': " + prefix + " " + resolved +
              " has no such file.";
  }
  return file;
}

namespace {
Manifest* g_project = nullptr;
}  // namespace

void setProjectDirectory(const std::string& directory) {
  delete g_project;
  g_project = new Manifest(loadManifest(directory));
}

const Manifest& projectManifest() {
  if (g_project == nullptr) setProjectDirectory(".");
  return *g_project;
}

// ---------------------------------------------------------------------
// The commands

namespace {

void note(const char* format, ...) {
  va_list args;
  va_start(args, format);
  std::vfprintf(stdout, format, args);
  va_end(args);
  std::fputc('\n', stdout);
}

void problem(const char* format, ...) {
  std::fputs("red pkg: ", stderr);
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
  std::fputc('\n', stderr);
}

// Requires a manifest, and says what to do when there is none.
bool requireManifest(Manifest* manifest) {
  *manifest = loadManifest(".");
  if (!manifest->loaded) {
    problem("no red.mod here or in any directory above. Run `red pkg init`.");
    return false;
  }
  return true;
}

// Every tag the remote publishes, newest first.
std::vector<std::string> remoteTags(const std::string& remote) {
  std::vector<std::string> tags;
  CommandResult result = run({"git", "ls-remote", "--tags", "--refs", remote},
                             false);
  if (result.status != 0) return tags;
  for (const std::string& line : splitOn(result.output, '\n')) {
    size_t marker = line.find("refs/tags/");
    if (marker == std::string::npos) continue;
    tags.push_back(trim(line.substr(marker + std::strlen("refs/tags/"))));
  }
  std::sort(tags.begin(), tags.end(),
            [](const std::string& a, const std::string& b) {
              return earlier(b, a);
            });
  return tags;
}

// What a requirement of `latest` means right now: the newest release
// tag, or the default branch when the repository has never tagged one.
std::string latestVersion(const std::string& remote) {
  std::vector<std::string> tags = remoteTags(remote);
  for (const std::string& tag : tags) {
    if (parseSemVer(tag).valid) return tag;
  }
  if (!tags.empty()) return tags.front();
  return "HEAD";
}

// Copies a directory tree. Used by `vendor`, which has to leave the
// cached copy alone.
bool copyTree(const std::string& from, const std::string& to,
              std::string* error) {
  if (!makeDirectories(directoryOf(to))) {
    if (error != nullptr) *error = "cannot make " + directoryOf(to);
    return false;
  }
  CommandResult result = run({"cp", "-R", from, to}, true);
  if (result.status != 0) {
    if (error != nullptr) *error = trim(result.output);
    return false;
  }
  return true;
}

bool removeTree(const std::string& path) {
  if (path.empty() || path == "/" || !isDirectory(path)) return false;
  return run({"rm", "-rf", path}, false).status == 0;
}

// Fetches one module at one version into the cache, unless it is
// already there. Fills in the lock entry either way.
bool fetch(const Manifest& manifest, const Requirement& requirement,
           LockEntry* entry, bool force) {
  if (!haveGit()) {
    problem("git is not on PATH, and it is how packages are fetched.");
    return false;
  }
  std::string remote = remoteFor(manifest, requirement.path);
  if (remote.empty()) {
    problem("cannot work out where '%s' comes from. Add a `source` line to red.mod.",
            requirement.path.c_str());
    return false;
  }

  std::string version = requirement.version;
  if (version.empty() || version == "latest") {
    note("  resolving %s", requirement.path.c_str());
    version = latestVersion(remote);
    if (version == "HEAD") {
      // No tags at all: pin the default branch's current commit, so the
      // build is still reproducible even though the request was not.
      CommandResult head = run({"git", "ls-remote", remote, "HEAD"}, false);
      std::vector<std::string> parts = fields(head.output);
      if (head.status != 0 || parts.empty()) {
        problem("cannot reach %s", remote.c_str());
        return false;
      }
      version = pseudoVersion(parts[0]);
    }
  }

  std::string directory = packageDirectory(requirement.path, version);
  if (directory.empty()) {
    problem("'%s' is not a usable import path.", requirement.path.c_str());
    return false;
  }

  entry->path = requirement.path;
  entry->version = version;
  entry->remote = remote;

  if (isDirectory(directory) && !force) {
    // Already in the cache. The commit is read back out of it rather
    // than from the network, so `red pkg get` on a warm cache is offline.
    CommandResult head =
        run({"git", "-C", directory, "rev-parse", "HEAD"}, false);
    entry->commit = head.status == 0 ? trim(head.output) : version;
    return true;
  }
  if (force) removeTree(directory);

  if (!makeDirectories(directoryOf(directory))) {
    problem("cannot make the cache directory %s", directoryOf(directory).c_str());
    return false;
  }

  note("  fetching %s %s", requirement.path.c_str(), version.c_str());

  // Cloned into a temporary name next door and moved into place once it
  // is complete, so an interrupted download never leaves a cache entry
  // that looks finished.
  std::string staging = directory + ".partial";
  removeTree(staging);

  // A pseudo-version names a commit, which a shallow clone of a tag
  // cannot reach, so those are fetched in full.
  bool isPseudo = startsWith(version, "v0.0.0-");
  std::vector<std::string> clone = {"git", "clone", "--quiet"};
  if (!isPseudo) {
    clone.push_back("--depth");
    clone.push_back("1");
    clone.push_back("--branch");
    clone.push_back(version);
  }
  clone.push_back(remote);
  clone.push_back(staging);

  CommandResult cloned = run(clone, true);
  if (cloned.status != 0) {
    removeTree(staging);
    problem("cannot fetch %s %s: %s", requirement.path.c_str(), version.c_str(),
            trim(cloned.output).c_str());
    return false;
  }
  if (isPseudo) {
    std::string commit = version.substr(std::strlen("v0.0.0-"));
    CommandResult checkout =
        run({"git", "-C", staging, "checkout", "--quiet", commit}, true);
    if (checkout.status != 0) {
      removeTree(staging);
      problem("cannot check out %s in %s: %s", commit.c_str(),
              requirement.path.c_str(), trim(checkout.output).c_str());
      return false;
    }
  }

  CommandResult head = run({"git", "-C", staging, "rev-parse", "HEAD"}, false);
  entry->commit = head.status == 0 ? trim(head.output) : version;

  // The history is not needed once the code is here, and a cache of
  // dependencies is much smaller without it.
  removeTree(joinPath(staging, ".git"));

  if (::rename(staging.c_str(), directory.c_str()) != 0) {
    removeTree(staging);
    problem("cannot move the download into %s", directory.c_str());
    return false;
  }
  return true;
}

// Reads the requirements of a package that is already in the cache, so
// that its own dependencies are fetched too. A package that ships no
// red.mod simply has none.
std::vector<Requirement> nestedRequirements(const std::string& directory) {
  std::vector<Requirement> out;
  std::string file = joinPath(directory, "red.mod");
  if (!fileExists(file)) return out;
  Manifest nested = loadManifest(directory);
  if (!nested.loaded) return out;
  for (Requirement requirement : nested.requirements) {
    requirement.direct = false;
    out.push_back(requirement);
  }
  return out;
}

// Fetches everything in the manifest, following what those packages
// require in turn. Breadth first, and a path already resolved is never
// resolved again, so a diamond is fetched once and a cycle terminates.
bool fetchAll(Manifest& manifest, bool force, std::vector<LockEntry>* lock) {
  std::set<std::string> seen;
  std::vector<Requirement> queue = manifest.requirements;
  bool manifestChanged = false;
  bool ok = true;

  for (size_t i = 0; i < queue.size(); i++) {
    const Requirement& requirement = queue[i];
    if (!seen.insert(requirement.path).second) continue;

    LockEntry entry;
    if (!fetch(manifest, requirement, &entry, force)) {
      ok = false;
      continue;
    }
    lock->push_back(entry);

    // A transitive requirement is recorded in this project's manifest
    // too, marked indirect. Flattening it here is what keeps resolution
    // at import time a single table lookup rather than a graph walk.
    if (manifest.find(requirement.path) == nullptr) {
      Requirement recorded = requirement;
      recorded.version = entry.version;
      recorded.direct = false;
      manifest.requirements.push_back(recorded);
      manifestChanged = true;
    }

    for (const Requirement& nested :
         nestedRequirements(packageDirectory(entry.path, entry.version))) {
      if (seen.count(nested.path) == 0) queue.push_back(nested);
    }
  }

  if (manifestChanged) {
    std::string error;
    if (!writeManifest(manifest, &error)) problem("%s", error.c_str());
  }
  return ok;
}

// Every import path a project's own .red files mention. Used by `tidy`.
std::set<std::string> importedPackagePaths(const std::string& root) {
  std::set<std::string> paths;
  // Reading the files with the compiler would be exact, but it would
  // also mean the tool cannot run on a project that does not compile.
  // A scan for import lines is what `tidy` wants anyway.
  CommandResult found = run({"find", root, "-name", "*.red", "-not", "-path",
                             joinPath(root, "vendor") + "/*"},
                            false);
  if (found.status != 0) return paths;
  for (const std::string& file : splitOn(found.output, '\n')) {
    std::string path = trim(file);
    if (path.empty()) continue;
    std::string source;
    if (!readFile(path, &source)) continue;
    for (const std::string& rawLine : splitOn(source, '\n')) {
      std::string line = trim(rawLine);
      if (!startsWith(line, "import")) continue;
      size_t open = line.find('"');
      if (open == std::string::npos) continue;
      size_t close = line.find('"', open + 1);
      if (close == std::string::npos) continue;
      std::string request = line.substr(open + 1, close - open - 1);
      if (looksLikePackagePath(request)) paths.insert(request);
    }
  }
  return paths;
}

int commandInit(const std::vector<std::string>& args) {
  std::string root = absolutePath(".");
  if (fileExists(joinPath(root, "red.mod"))) {
    problem("there is already a red.mod here.");
    return 1;
  }
  std::string modulePath;
  if (args.size() >= 1) {
    modulePath = args[0];
  } else {
    // A local project still needs a name to import itself by. The
    // directory's own name under a reserved host is a name that cannot
    // be confused with a fetchable one.
    modulePath = "local/" + root.substr(root.find_last_of('/') + 1);
  }

  Manifest manifest;
  manifest.file = joinPath(root, "red.mod");
  manifest.root = root;
  manifest.modulePath = modulePath;
  manifest.redVersion = kVersion;

  std::string error;
  if (!writeManifest(manifest, &error)) {
    problem("%s", error.c_str());
    return 1;
  }
  note("Wrote red.mod for module %s", modulePath.c_str());
  note("Add a dependency with `red pkg add github.com/owner/repo`.");
  return 0;
}

int commandAdd(const std::vector<std::string>& args) {
  if (args.empty()) {
    problem("usage: red pkg add <path>[@version] ...");
    return 2;
  }
  Manifest manifest;
  if (!requireManifest(&manifest)) return 1;

  for (const std::string& argument : args) {
    std::string path = argument;
    std::string version = "latest";
    size_t at = argument.rfind('@');
    // The @ has to come after the host, so an SSH-looking path is not
    // mistaken for a version.
    if (at != std::string::npos && at > argument.find('/')) {
      path = argument.substr(0, at);
      version = argument.substr(at + 1);
    }
    if (!looksLikePackagePath(path)) {
      problem("'%s' is not a package path. It should look like github.com/owner/repo.",
              path.c_str());
      return 1;
    }
    bool replaced = false;
    for (Requirement& requirement : manifest.requirements) {
      if (requirement.path == path) {
        requirement.version = version;
        requirement.direct = true;
        replaced = true;
      }
    }
    if (!replaced) {
      Requirement requirement;
      requirement.path = path;
      requirement.version = version;
      manifest.requirements.push_back(requirement);
    }
    note("require %s %s", path.c_str(), version.c_str());
  }

  std::vector<LockEntry> lock;
  bool ok = fetchAll(manifest, false, &lock);

  // `latest` meant something at the moment it was asked. What it meant
  // is written back, so the manifest names a version rather than a wish.
  for (Requirement& requirement : manifest.requirements) {
    if (requirement.version != "latest") continue;
    for (const LockEntry& entry : lock) {
      if (entry.path == requirement.path) requirement.version = entry.version;
    }
  }

  std::string error;
  if (!writeManifest(manifest, &error)) {
    problem("%s", error.c_str());
    return 1;
  }
  if (!writeLock(manifest.root, lock, &error)) {
    problem("%s", error.c_str());
    return 1;
  }
  return ok ? 0 : 1;
}

int commandRemove(const std::vector<std::string>& args) {
  if (args.empty()) {
    problem("usage: red pkg remove <path> ...");
    return 2;
  }
  Manifest manifest;
  if (!requireManifest(&manifest)) return 1;

  for (const std::string& path : args) {
    size_t before = manifest.requirements.size();
    manifest.requirements.erase(
        std::remove_if(manifest.requirements.begin(),
                       manifest.requirements.end(),
                       [&](const Requirement& r) { return r.path == path; }),
        manifest.requirements.end());
    if (manifest.requirements.size() == before) {
      problem("'%s' is not required by this project.", path.c_str());
    } else {
      note("dropped %s", path.c_str());
    }
  }

  std::vector<LockEntry> lock;
  fetchAll(manifest, false, &lock);
  std::string error;
  if (!writeManifest(manifest, &error) ||
      !writeLock(manifest.root, lock, &error)) {
    problem("%s", error.c_str());
    return 1;
  }
  return 0;
}

int commandGet(const std::vector<std::string>& args) {
  bool force = false;
  for (const std::string& argument : args) {
    if (argument == "--force" || argument == "-f") force = true;
  }
  Manifest manifest;
  if (!requireManifest(&manifest)) return 1;
  if (manifest.requirements.empty()) {
    note("Nothing to fetch: red.mod has no requirements.");
    return 0;
  }
  note("Fetching %zu package%s", manifest.requirements.size(),
       manifest.requirements.size() == 1 ? "" : "s");

  std::vector<LockEntry> lock;
  bool ok = fetchAll(manifest, force, &lock);
  std::string error;
  if (!writeLock(manifest.root, lock, &error)) {
    problem("%s", error.c_str());
    return 1;
  }
  if (ok) note("Up to date.");
  return ok ? 0 : 1;
}

int commandUpdate(const std::vector<std::string>& args) {
  Manifest manifest;
  if (!requireManifest(&manifest)) return 1;

  std::set<std::string> wanted(args.begin(), args.end());
  for (Requirement& requirement : manifest.requirements) {
    if (!wanted.empty() && wanted.count(requirement.path) == 0) continue;
    // Only a pin that was never exact is moved. A project that asked for
    // v1.2.0 gets v1.2.0 until somebody edits red.mod.
    if (parseSemVer(requirement.version).valid &&
        !startsWith(requirement.version, "v0.0.0-")) {
      continue;
    }
    requirement.version = "latest";
  }

  std::vector<LockEntry> lock;
  bool ok = fetchAll(manifest, true, &lock);

  // The versions that `latest` resolved to are written back, so the
  // manifest keeps saying what is actually in use.
  for (Requirement& requirement : manifest.requirements) {
    for (const LockEntry& entry : lock) {
      if (entry.path == requirement.path && requirement.version == "latest") {
        requirement.version = entry.version;
      }
    }
  }
  std::string error;
  if (!writeManifest(manifest, &error) ||
      !writeLock(manifest.root, lock, &error)) {
    problem("%s", error.c_str());
    return 1;
  }
  return ok ? 0 : 1;
}

int commandList(const std::vector<std::string>& args) {
  Manifest manifest;
  if (!requireManifest(&manifest)) return 1;
  note("module %s", manifest.modulePath.c_str());
  if (manifest.requirements.empty()) {
    note("  (no requirements)");
    return 0;
  }
  std::vector<LockEntry> lock = loadLock(manifest.root);
  bool verbose = std::find(args.begin(), args.end(), "-v") != args.end();

  for (const Requirement& requirement : manifest.requirements) {
    const LockEntry* locked = nullptr;
    for (const LockEntry& entry : lock) {
      if (entry.path == requirement.path) locked = &entry;
    }
    std::string version = locked != nullptr ? locked->version
                                            : requirement.version;
    std::string state =
        isDirectory(packageDirectory(requirement.path, version))
            ? ""
            : "  (not downloaded)";
    note("  %-44s %-18s%s%s", requirement.path.c_str(), version.c_str(),
         requirement.direct ? "" : " indirect", state.c_str());
    if (verbose && locked != nullptr) {
      note("      %s  %s", locked->commit.c_str(), locked->remote.c_str());
    }
  }
  return 0;
}

int commandTidy(const std::vector<std::string>&) {
  Manifest manifest;
  if (!requireManifest(&manifest)) return 1;

  std::set<std::string> imported = importedPackagePaths(manifest.root);

  // An import of a package inside a repository counts as a use of the
  // requirement that covers it.
  std::set<std::string> used;
  std::vector<std::string> missing;
  for (const std::string& request : imported) {
    if (!manifest.modulePath.empty() &&
        (request == manifest.modulePath ||
         startsWith(request, manifest.modulePath + "/"))) {
      continue;
    }
    bool covered = false;
    for (const Requirement& requirement : manifest.requirements) {
      if (request == requirement.path ||
          startsWith(request, requirement.path + "/")) {
        used.insert(requirement.path);
        covered = true;
      }
    }
    if (!covered) missing.push_back(request);
  }

  // Something only a dependency needs stays, marked indirect.
  for (Requirement& requirement : manifest.requirements) {
    bool directlyUsed = used.count(requirement.path) > 0;
    if (requirement.direct && !directlyUsed) {
      note("  %s is no longer imported directly", requirement.path.c_str());
      requirement.direct = false;
    } else if (!requirement.direct && directlyUsed) {
      requirement.direct = true;
    }
  }

  for (const std::string& request : missing) {
    // Only the repository part becomes a requirement; the rest of the
    // path is a directory inside it.
    std::vector<std::string> segments = splitOn(request, '/');
    std::string repository = segments.size() >= 3
                                 ? segments[0] + "/" + segments[1] + "/" +
                                       segments[2]
                                 : request;
    if (manifest.find(repository) != nullptr) continue;
    Requirement requirement;
    requirement.path = repository;
    requirement.version = "latest";
    manifest.requirements.push_back(requirement);
    note("  + %s (imported but not required)", repository.c_str());
  }

  std::vector<LockEntry> lock;
  bool ok = fetchAll(manifest, false, &lock);

  // Anything the lock still names but the manifest no longer requires is
  // dropped from the lock as well.
  lock.erase(std::remove_if(lock.begin(), lock.end(),
                            [&](const LockEntry& entry) {
                              return manifest.find(entry.path) == nullptr;
                            }),
             lock.end());

  std::string error;
  if (!writeManifest(manifest, &error) ||
      !writeLock(manifest.root, lock, &error)) {
    problem("%s", error.c_str());
    return 1;
  }
  note("Tidy.");
  return ok ? 0 : 1;
}

int commandVendor(const std::vector<std::string>&) {
  Manifest manifest;
  if (!requireManifest(&manifest)) return 1;

  std::vector<LockEntry> lock;
  if (!fetchAll(manifest, false, &lock)) return 1;

  std::string vendor = joinPath(manifest.root, "vendor");
  removeTree(vendor);
  if (!makeDirectories(vendor)) {
    problem("cannot make %s", vendor.c_str());
    return 1;
  }

  for (const LockEntry& entry : lock) {
    std::string from = packageDirectory(entry.path, entry.version);
    std::string to = joinPath(vendor, entry.path);
    std::string error;
    if (!copyTree(from, to, &error)) {
      problem("cannot vendor %s: %s", entry.path.c_str(), error.c_str());
      return 1;
    }
    note("  vendored %s %s", entry.path.c_str(), entry.version.c_str());
  }
  note("%zu package%s in vendor/. Imports come from there now.", lock.size(),
       lock.size() == 1 ? "" : "s");
  return 0;
}

int commandCache(const std::vector<std::string>& args) {
  std::string action = args.empty() ? "dir" : args[0];
  if (action == "dir") {
    note("%s", packageCacheRoot().c_str());
    return 0;
  }
  if (action == "clean") {
    std::string root = packageCacheRoot();
    if (!isDirectory(root)) {
      note("Nothing cached.");
      return 0;
    }
    if (!removeTree(root)) {
      problem("cannot remove %s", root.c_str());
      return 1;
    }
    note("Removed %s", root.c_str());
    return 0;
  }
  problem("usage: red pkg cache [dir|clean]");
  return 2;
}

int commandWhere(const std::vector<std::string>& args) {
  if (args.empty()) {
    problem("usage: red pkg where <import path>");
    return 2;
  }
  Manifest manifest = loadManifest(".");
  std::string reason;
  std::string file = resolvePackageImport(manifest, args[0], &reason);
  if (file.empty()) {
    problem("%s", reason.empty() ? "not a package path" : reason.c_str());
    return 1;
  }
  note("%s", file.c_str());
  return 0;
}

void printPackageUsage() {
  std::printf(
      "Usage: red pkg <command> [arguments]\n"
      "\n"
      "Red has no package registry. A dependency is named by where it\n"
      "lives, and that name is both the import path and the address it\n"
      "is fetched from.\n"
      "\n"
      "  init [module path]     Start a project. Writes red.mod.\n"
      "  add <path>[@version]   Require a package and fetch it.\n"
      "  remove <path>          Stop requiring a package.\n"
      "  get [--force]          Fetch everything red.mod requires.\n"
      "  update [path ...]      Move unpinned requirements forward.\n"
      "  list [-v]              Show what this project requires.\n"
      "  tidy                   Match red.mod to what the code imports.\n"
      "  vendor                 Copy dependencies into vendor/.\n"
      "  where <path>           Print the file an import resolves to.\n"
      "  cache [dir|clean]      Inspect or empty the download cache.\n"
      "\n"
      "A version may be a tag, a branch, a commit, or `latest`.\n");
}

}  // namespace

int packageCommand(const std::vector<std::string>& args) {
  if (args.empty() || args[0] == "help" || args[0] == "--help") {
    printPackageUsage();
    return args.empty() ? 2 : 0;
  }
  std::vector<std::string> rest(args.begin() + 1, args.end());
  const std::string& command = args[0];

  if (command == "init") return commandInit(rest);
  if (command == "add") return commandAdd(rest);
  if (command == "remove" || command == "rm") return commandRemove(rest);
  if (command == "get" || command == "sync") return commandGet(rest);
  if (command == "update" || command == "upgrade") return commandUpdate(rest);
  if (command == "list" || command == "ls") return commandList(rest);
  if (command == "tidy") return commandTidy(rest);
  if (command == "vendor") return commandVendor(rest);
  if (command == "where") return commandWhere(rest);
  if (command == "cache") return commandCache(rest);

  problem("unknown command '%s'. Try `red pkg help`.", command.c_str());
  return 2;
}

}  // namespace red
