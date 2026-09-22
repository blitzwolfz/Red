// Files and directories, above what the built-ins do.
//
//   import "std/fs" as fs;
//
// `read_file`, `write_file`, `list_dir`, `mkdir` and the rest are
// built in; docs/stdlib.md lists them. What is here is the layer that
// otherwise gets written again in every program: walking a tree, making
// a directory and its parents, writing a file without leaving a
// half-written one behind.

import "std/path" as path;
import "std/strings" as strings;

// Makes a directory and every directory above it. Does nothing when it
// is already there.
fun ensure_dir(target) {
  if (is_dir(target)) { return target; }
  const parent = path.dir(target);
  if (parent != target and parent != "." and parent != "/" and
      !is_dir(parent)) {
    ensure_dir(parent);
  }
  if (!is_dir(target)) { mkdir(target); }
  return target;
}

// The whole file, raising when it cannot be read.
//
// `read_file` returns nil instead, which is right for a caller that has
// something else to do about it, and wrong for one that does not: the
// nil turns up later as a puzzling error about a method on nil.
fun read(target) {
  const text = read_file(target);
  if (text == nil) {
    throw error("cannot read '${target}'", target, "io");
  }
  return text;
}

// Every line of a file, terminators removed.
fun read_lines(target) { return strings.lines(read(target)); }

// Writes lines, each with a newline after it, including the last.
fun write_lines(target, lines) {
  let text = "";
  for (let line in lines) { text += str(line) + "\n"; }
  write_file(target, text);
  return target;
}

// Writes through a temporary file next door and renames it into place,
// so a reader sees either the old contents or the new ones and never
// half of the new ones. Worth it for anything a running program reads.
fun write_atomic(target, text) {
  const temporary = "${target}.tmp${rand(100000)}";
  write_file(temporary, text);
  rename(temporary, target);
  return target;
}

// The whole file appended to, made if it is not there.
fun append_line(target, line) {
  append_file(target, str(line) + "\n");
  return target;
}

fun copy_file(from, to) {
  ensure_dir(path.dir(to));
  write_file(to, read(from));
  return to;
}

// Every entry under a directory, deepest last, as full paths.
//
// `options` may hold `files` and `dirs` (both true by default) and
// `skip`, a function given a path that returns true to leave it out --
// which for a directory leaves out everything under it too.
fun walk(root, options = nil) {
  let wantFiles = true;
  let wantDirs = true;
  let skip = nil;
  if (options != nil) {
    wantFiles = options.get("files", true);
    wantDirs = options.get("dirs", true);
    skip = options.get("skip", nil);
  }

  let out = [];
  let pending = [root];
  while (pending.len() > 0) {
    const directory = pending.pop();
    for (let name in list_dir(directory).sort()) {
      const full = path.join(directory, name);
      if (skip != nil and skip(full)) { continue; }
      if (is_dir(full)) {
        if (wantDirs) { out.push(full); }
        pending.push(full);
      } else if (wantFiles) {
        out.push(full);
      }
    }
  }
  return out;
}

// Files under a directory whose names end with one of `extensions`.
//
//   fs.with_ext("src", [".cpp", ".h"])
fun with_ext(root, extensions) {
  let out = [];
  for (let file in walk(root, {"dirs": false})) {
    if (extensions.contains(path.ext(file))) { out.push(file); }
  }
  return out;
}

// A directory and everything in it. Refuses a path that is not a
// directory, so a mistyped argument cannot take a file with it.
fun remove_tree(root) {
  if (!is_dir(root)) {
    throw error("'${root}' is not a directory", root, "io");
  }
  let entries = walk(root);
  // Deepest first, because a directory has to be empty before it goes.
  let i = entries.len() - 1;
  while (i >= 0) {
    const entry = entries[i];
    if (is_dir(entry)) { remove_dir(entry); } else { remove_file(entry); }
    i -= 1;
  }
  remove_dir(root);
  return root;
}

// How many bytes a tree holds.
fun tree_size(root) {
  let total = 0;
  for (let file in walk(root, {"dirs": false})) { total += file_size(file); }
  return total;
}

// A temporary directory of this program's own, made under `parent`.
// Nothing removes it: the caller decides when, because the usual reason
// to make one is to keep something past the end of a function.
fun temp_dir(prefix = "red", parent = nil) {
  let base = parent;
  if (base == nil) { base = env("TMPDIR", "/tmp"); }
  for (let attempt in range(0, 100)) {
    const candidate = path.join(base, "${prefix}-${rand(1000000000)}");
    if (!exists(candidate)) {
      mkdir(candidate);
      return candidate;
    }
  }
  throw error("cannot make a temporary directory under '${base}'", base, "io");
}

// Does a file's name match a shell-style pattern? Supports "*", "?" and
// character classes, which is what a file filter actually needs.
fun matches(name, pattern) {
  return glob_at(name, 0, pattern, 0);
}

fun glob_at(name, i, pattern, j) {
  while (j < pattern.len()) {
    const p = pattern[j];
    if (p == "*") {
      // Try every length the star could cover, shortest first.
      for (let k in range(i, name.len() + 1)) {
        if (glob_at(name, k, pattern, j + 1)) { return true; }
      }
      return false;
    }
    if (i >= name.len()) { return false; }
    if (p == "?") {
      i += 1;
      j += 1;
      continue;
    }
    if (p == "[") {
      const close = pattern.sub(j).find("]");
      if (close < 0) { return false; }
      let set = pattern.sub(j + 1, j + close);
      let negated = false;
      if (set.starts_with("!") or set.starts_with("^")) {
        negated = true;
        set = set.sub(1);
      }
      let hit = in_class(set, name[i]);
      if (negated) { hit = !hit; }
      if (!hit) { return false; }
      i += 1;
      j += close + 1;
      continue;
    }
    if (p != name[i]) { return false; }
    i += 1;
    j += 1;
  }
  return i == name.len();
}

// One character against the body of a "[...]", with ranges: "a-z0-9_".
fun in_class(set, c) {
  let i = 0;
  while (i < set.len()) {
    if (i + 2 < set.len() and set[i + 1] == "-") {
      if (c >= set[i] and c <= set[i + 2]) { return true; }
      i += 3;
      continue;
    }
    if (set[i] == c) { return true; }
    i += 1;
  }
  return false;
}

// Files under a directory matching a pattern.
fun glob(root, pattern) {
  let out = [];
  for (let file in walk(root, {"dirs": false})) {
    if (matches(path.base(file), pattern)) { out.push(file); }
  }
  return out;
}
