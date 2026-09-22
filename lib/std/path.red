// File paths, as text.
//
//   import "std/path" as path;
//
// Nothing here touches the file system: these are string operations on
// something shaped like a path. std/fs is the half that reads and writes.
// Red targets POSIX systems, so the separator is "/".

const SEPARATOR = "/";

fun is_absolute(p) { return p.starts_with(SEPARATOR); }

// Joins parts with exactly one separator between them. An absolute part
// wins, discarding everything before it, which is what makes
// join(base, userSupplied) behave predictably when the second is
// absolute rather than silently producing a path under base.
fun join(...parts) {
  let out = "";
  for (let part in parts) {
    if (part == "" or part == nil) { continue; }
    if (out == "" or is_absolute(part)) {
      out = part;
    } else if (out.ends_with(SEPARATOR)) {
      out += part;
    } else {
      out += SEPARATOR + part;
    }
  }
  return out;
}

// Everything before the last separator. "." when there is none, so the
// result is always usable as a directory.
fun dir(p) {
  const trimmed = strip_trailing(p);
  let at = -1;
  for (let i in range(0, trimmed.len())) {
    if (trimmed[i] == SEPARATOR) { at = i; }
  }
  if (at < 0) { return "."; }
  if (at == 0) { return SEPARATOR; }
  return trimmed.sub(0, at);
}

// Everything after the last separator.
fun base(p) {
  const trimmed = strip_trailing(p);
  let at = -1;
  for (let i in range(0, trimmed.len())) {
    if (trimmed[i] == SEPARATOR) { at = i; }
  }
  if (at < 0) { return trimmed; }
  return trimmed.sub(at + 1);
}

// The extension, dot included: ".red". Empty when there is none. A
// leading dot does not count, so ".gitignore" has no extension, which is
// what anyone asking actually means.
fun ext(p) {
  const name = base(p);
  let at = -1;
  for (let i in range(0, name.len())) {
    if (name[i] == ".") { at = i; }
  }
  if (at <= 0) { return ""; }
  return name.sub(at);
}

// The file name without its extension.
fun stem(p) {
  const name = base(p);
  const extension = ext(p);
  if (extension == "") { return name; }
  return name.sub(0, name.len() - extension.len());
}

// The same path with a different extension. Pass "" to remove it.
fun with_ext(p, extension) {
  const parent = dir(p);
  let name = stem(p) + extension;
  if (parent == ".") { return name; }
  return join(parent, name);
}

fun strip_trailing(p) {
  if (p == SEPARATOR) { return p; }
  let end = p.len();
  while (end > 1 and p[end - 1] == SEPARATOR) { end -= 1; }
  return p.sub(0, end);
}

// Resolves "." and ".." without looking at the file system, so it works
// on a path that does not exist yet. A ".." that would climb above the
// root of a relative path is kept, because "../x" means something.
fun clean(p) {
  if (p == "") { return "."; }
  const absolute = is_absolute(p);
  let parts = [];
  for (let piece in p.split(SEPARATOR)) {
    if (piece == "" or piece == ".") { continue; }
    if (piece == "..") {
      if (parts.len() > 0 and parts[parts.len() - 1] != "..") {
        parts.pop();
        continue;
      }
      if (absolute) { continue; }
    }
    parts.push(piece);
  }
  const joined = parts.join(SEPARATOR);
  if (absolute) { return SEPARATOR + joined; }
  if (joined == "") { return "."; }
  return joined;
}

// The path of `target` as seen from `from`. Both are cleaned first.
// Returns the target unchanged when one is absolute and the other is
// not, because there is no honest answer in that case.
fun relative(from, target) {
  if (is_absolute(from) != is_absolute(target)) { return clean(target); }
  const a = split(clean(from));
  const b = split(clean(target));
  let common = 0;
  while (common < a.len() and common < b.len() and a[common] == b[common]) {
    common += 1;
  }
  let parts = [];
  for (let i in range(common, a.len())) { parts.push(".."); }
  for (let i in range(common, b.len())) { parts.push(b[i]); }
  if (parts.len() == 0) { return "."; }
  return parts.join(SEPARATOR);
}

// The parts of a path, with empty pieces dropped.
fun split(p) {
  let parts = [];
  for (let piece in p.split(SEPARATOR)) {
    if (piece != "" and piece != ".") { parts.push(piece); }
  }
  return parts;
}

// Is `child` inside `parent`? Both are cleaned first, so this is not
// fooled by "a/../b". It is a text answer, not a security check: it
// knows nothing about symbolic links.
fun contains(parent, child) {
  const a = clean(parent);
  const b = clean(child);
  if (a == b) { return true; }
  let prefix = a + SEPARATOR;
  if (a.ends_with(SEPARATOR)) { prefix = a; }
  return b.starts_with(prefix);
}
