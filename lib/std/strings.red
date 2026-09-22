// Text, beyond what a string already does for itself.
//
//   import "std/strings" as strings;
//
// A string already knows how to trim, split, find, replace, pad and
// case itself; docs/stdlib.md lists those. What is here is the next
// layer: splitting that stops, cutting on the first separator, wrapping,
// and the small predicates that otherwise get written again in every
// program.

const WHITESPACE = " \t\n\r";

fun is_space(c) { return WHITESPACE.contains(c); }

fun is_digit(c) {
  if (c.len() != 1) { return false; }
  const code = c.code_at(0);
  return code >= 48 and code <= 57;
}

fun is_alpha(c) {
  if (c.len() != 1) { return false; }
  const code = c.code_at(0);
  return (code >= 65 and code <= 90) or (code >= 97 and code <= 122);
}

fun is_alnum(c) { return is_alpha(c) or is_digit(c); }

// Only the ASCII ones. Anything else is already the caller's business,
// because "is this a letter" past ASCII depends on what they are for.
fun is_hex(c) {
  if (c.len() != 1) { return false; }
  const code = c.code_at(0);
  return is_digit(c) or (code >= 65 and code <= 70) or
         (code >= 97 and code <= 102);
}

fun is_blank(text) { return text.trim() == ""; }

// Splits on the first separator only, and gives back both halves. The
// usual case for a header line, a key=value pair or a path.
//
// Returns [whole, ""] when the separator is not there, so the first half
// is always everything the caller has.
fun cut(text, separator) {
  const at = text.find(separator);
  if (at < 0) { return [text, ""]; }
  return [text.sub(0, at), text.sub(at + separator.len())];
}

// The same from the other end.
fun cut_last(text, separator) {
  let at = -1;
  let from = 0;
  for (;;) {
    const found = text.sub(from).find(separator);
    if (found < 0) { break; }
    at = from + found;
    from = at + separator.len();
  }
  if (at < 0) { return [text, ""]; }
  return [text.sub(0, at), text.sub(at + separator.len())];
}

// split(), but stopping after `limit` pieces. The last piece keeps
// whatever is left, separators and all.
fun split_n(text, separator, limit) {
  if (limit <= 0) { return text.split(separator); }
  let pieces = [];
  let rest = text;
  while (pieces.len() < limit - 1) {
    const at = rest.find(separator);
    if (at < 0) { break; }
    pieces.push(rest.sub(0, at));
    rest = rest.sub(at + separator.len());
  }
  pieces.push(rest);
  return pieces;
}

// Every line, with the terminators removed. A trailing newline does not
// produce an empty last line, because a file that ends properly is not a
// file with an extra blank line in it.
fun lines(text) {
  const normalised = text.replace("\r\n", "\n");
  if (normalised == "") { return []; }
  const pieces = normalised.split("\n");
  if (pieces.len() > 0 and pieces[pieces.len() - 1] == "") { pieces.pop(); }
  return pieces;
}

// Runs of non-whitespace, with the whitespace discarded.
fun words(text) {
  let out = [];
  let current = "";
  for (let c in text) {
    if (is_space(c)) {
      if (current != "") { out.push(current); }
      current = "";
    } else {
      current += c;
    }
  }
  if (current != "") { out.push(current); }
  return out;
}

fun count(text, needle) {
  if (needle == "") { return 0; }
  let total = 0;
  let from = 0;
  for (;;) {
    const at = text.sub(from).find(needle);
    if (at < 0) { break; }
    total += 1;
    from = from + at + needle.len();
  }
  return total;
}

fun replace_first(text, from, to) {
  const at = text.find(from);
  if (at < 0) { return text; }
  return text.sub(0, at) + to + text.sub(at + from.len());
}

// The prefix or suffix removed if it is there, and the string unchanged
// if it is not. Saves the `if` that otherwise wraps every use.
fun without_prefix(text, prefix) {
  if (!text.starts_with(prefix)) { return text; }
  return text.sub(prefix.len());
}

fun without_suffix(text, suffix) {
  if (!text.ends_with(suffix)) { return text; }
  return text.sub(0, text.len() - suffix.len());
}

fun trim_chars(text, chars) {
  let start = 0;
  let end = text.len();
  while (start < end and chars.contains(text[start])) { start += 1; }
  while (end > start and chars.contains(text[end - 1])) { end -= 1; }
  return text.sub(start, end);
}

fun center(text, width, fill = " ") {
  const gap = width - text.char_len();
  if (gap <= 0) { return text; }
  const left = int(gap / 2);
  return fill.repeat(left) + text + fill.repeat(gap - left);
}

// First letter of every word in upper case, the rest left alone. Not a
// linguistic title case: it knows nothing about "of" and "the".
fun title(text) {
  let out = "";
  let atStart = true;
  for (let c in text) {
    if (is_space(c) or c == "-" or c == "_") {
      atStart = true;
      out += c;
    } else if (atStart) {
      out += c.upper();
      atStart = false;
    } else {
      out += c;
    }
  }
  return out;
}

// Wraps at whitespace to a width, in characters. A word longer than the
// width goes on a line of its own rather than being cut in half.
fun wrap(text, width = 72) {
  let out = [];
  let line = "";
  for (let word in words(text)) {
    if (line == "") {
      line = word;
    } else if (line.char_len() + 1 + word.char_len() <= width) {
      line += " " + word;
    } else {
      out.push(line);
      line = word;
    }
  }
  if (line != "") { out.push(line); }
  return out;
}

// Every line prefixed. For quoting output inside a report.
fun indent(text, prefix = "  ") {
  let out = [];
  for (let line in lines(text)) { out.push(prefix + line); }
  return out.join("\n");
}

// Shortened to a width, with an ellipsis, when it is longer.
fun ellipsis(text, width, marker = "...") {
  if (text.char_len() <= width) { return text; }
  if (width <= marker.char_len()) { return marker.sub(0, width); }
  const keep = width - marker.char_len();
  return text.chars().slice(0, keep).join("") + marker;
}

// A comparison suitable for sort(), ordering "file2" before "file10" the
// way a person reading the names would.
fun natural_less(a, b) {
  let i = 0;
  let j = 0;
  while (i < a.len() and j < b.len()) {
    if (is_digit(a[i]) and is_digit(b[j])) {
      let x = "";
      let y = "";
      while (i < a.len() and is_digit(a[i])) { x += a[i]; i += 1; }
      while (j < b.len() and is_digit(b[j])) { y += b[j]; j += 1; }
      const nx = num(x);
      const ny = num(y);
      if (nx != ny) { return nx < ny; }
    } else {
      if (a[i] != b[j]) { return a[i] < b[j]; }
      i += 1;
      j += 1;
    }
  }
  return a.len() - i < b.len() - j;
}

// "1.5 kB", "2.3 MB". Powers of 1024, because this is for sizes on disk
// and in memory.
fun human_bytes(bytes) {
  const units = ["B", "kB", "MB", "GB", "TB", "PB"];
  let value = abs(bytes);
  let unit = 0;
  while (value >= 1024 and unit < units.len() - 1) {
    value = value / 1024;
    unit += 1;
  }
  let text = str(round(value * 10) / 10);
  if (unit == 0) { text = str(int(value)); }
  if (bytes < 0) { text = "-" + text; }
  return "${text} ${units[unit]}";
}
