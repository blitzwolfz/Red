// How wide a string is on a grid, and how to cut one to fit.
//
//   import "andy/text" as text;
//
// A cell is one column. Most characters take one, a combining mark takes
// none, and the characters used to write Chinese, Japanese and Korean —
// along with most emoji — take two. Measuring a string in bytes, or even
// in characters, puts a box border in the wrong column the first time
// somebody types a name in kanji, so everything andy draws is measured
// here instead.
//
// The ranges below follow Unicode's East Asian Width property and the
// usual treatment of combining marks. They are the same ranges every
// terminal library carries, and terminals do disagree about a handful of
// symbols at the edges; where they do, there is no answer that is right
// everywhere.

// How many columns one code point takes: 0, 1 or 2.
fun code_width(code) {
  if (code == 0) { return 0; }
  // C0 and C1 control characters are not drawn at all.
  if (code < 32) { return 0; }
  if (code >= 0x7f and code < 0xa0) { return 0; }

  // Combining marks, which attach to the character before them.
  if (is_combining(code)) { return 0; }

  if (is_wide(code)) { return 2; }
  return 1;
}

fun is_combining(code) {
  if (code < 0x300) { return false; }
  return (code >= 0x0300 and code <= 0x036f) or   // combining diacriticals
         (code >= 0x0483 and code <= 0x0489) or
         (code >= 0x0591 and code <= 0x05bd) or   // Hebrew points
         (code >= 0x0610 and code <= 0x061a) or   // Arabic marks
         (code >= 0x064b and code <= 0x065f) or
         (code == 0x0670) or
         (code >= 0x06d6 and code <= 0x06dc) or
         (code >= 0x0900 and code <= 0x0903) or   // Devanagari
         (code >= 0x093a and code <= 0x093c) or
         (code >= 0x0941 and code <= 0x094d) or
         (code == 0x0e31) or                      // Thai
         (code >= 0x0e34 and code <= 0x0e3a) or
         (code >= 0x0e47 and code <= 0x0e4e) or
         (code >= 0x1ab0 and code <= 0x1aff) or
         (code >= 0x1dc0 and code <= 0x1dff) or
         (code >= 0x20d0 and code <= 0x20f0) or   // marks for symbols
         (code >= 0xfe00 and code <= 0xfe0f) or   // variation selectors
         (code >= 0xfe20 and code <= 0xfe2f) or
         (code >= 0xe0100 and code <= 0xe01ef);
}

fun is_wide(code) {
  if (code < 0x1100) { return false; }
  return (code >= 0x1100 and code <= 0x115f) or   // Hangul Jamo
         (code >= 0x2e80 and code <= 0x303e) or   // CJK radicals, Kangxi
         (code >= 0x3041 and code <= 0x33ff) or   // kana through CJK symbols
         (code >= 0x3400 and code <= 0x4dbf) or   // CJK extension A
         (code >= 0x4e00 and code <= 0x9fff) or   // CJK unified
         (code >= 0xa000 and code <= 0xa4cf) or   // Yi
         (code >= 0xa960 and code <= 0xa97f) or
         (code >= 0xac00 and code <= 0xd7a3) or   // Hangul syllables
         (code >= 0xf900 and code <= 0xfaff) or   // CJK compatibility
         (code >= 0xfe10 and code <= 0xfe19) or
         (code >= 0xfe30 and code <= 0xfe6f) or
         (code >= 0xff00 and code <= 0xff60) or   // fullwidth forms
         (code >= 0xffe0 and code <= 0xffe6) or
         (code >= 0x16fe0 and code <= 0x16fe4) or
         (code >= 0x17000 and code <= 0x18aff) or // Tangut
         (code >= 0x1b000 and code <= 0x1b2ff) or
         (code == 0x1f004) or
         (code == 0x1f0cf) or
         (code >= 0x1f300 and code <= 0x1f64f) or // emoji
         (code >= 0x1f680 and code <= 0x1f6ff) or
         (code >= 0x1f900 and code <= 0x1f9ff) or
         (code >= 0x20000 and code <= 0x3fffd);   // CJK extensions B onward
}

// How many columns a whole string takes.
fun width(value) {
  let total = 0;
  for (let code in value.code_points()) { total += code_width(code); }
  return total;
}

// The string as an array of [character, width] pairs, with a combining
// mark folded into the character it belongs to. This is the form the
// canvas draws from: one entry is one thing that occupies a position,
// however many code points went into it.
fun clusters(value) {
  const out = [];
  for (let character in value.chars()) {
    const w = code_width(character.code_points()[0]);
    if (w == 0 and out.len() > 0) {
      // A mark joins the character before it rather than standing alone,
      // so that deleting one character deletes the accent with it.
      out[out.len() - 1][0] += character;
    } else if (w == 0) {
      // A mark with nothing before it: keep it, so no text is lost, but
      // give it a column of its own since there is nothing to attach to.
      out.push([character, 1]);
    } else {
      out.push([character, w]);
    }
  }
  return out;
}

// The longest prefix of `value` that fits in `columns`.
//
// A double width character that would half hang over the edge is left
// out: half of one is not a character, and a terminal asked to draw one
// does something different in every terminal.
fun truncate(value, columns) {
  if (columns <= 0) { return ""; }
  let used = 0;
  let out = "";
  for (let [character, w] in clusters(value)) {
    if (used + w > columns) { break; }
    out += character;
    used += w;
  }
  return out;
}

// The same, with `ellipsis` on the end when something was cut off. The
// ellipsis is measured too, so the result really does fit.
fun ellipsize(value, columns, ellipsis = "…") {
  if (width(value) <= columns) { return value; }
  const room = columns - width(ellipsis);
  if (room <= 0) { return truncate(ellipsis, columns); }
  return truncate(value, room) + ellipsis;
}

// Padded on the right to `columns`, measured in columns rather than in
// bytes. This is what lines up a table.
fun pad_right(value, columns, fill = " ") {
  const missing = columns - width(value);
  if (missing <= 0) { return value; }
  return value + fill.repeat(missing);
}

fun pad_left(value, columns, fill = " ") {
  const missing = columns - width(value);
  if (missing <= 0) { return value; }
  return fill.repeat(missing) + value;
}

// Centred, with any odd column going to the right, which is where the
// eye expects it.
fun center(value, columns, fill = " ") {
  const missing = columns - width(value);
  if (missing <= 0) { return value; }
  const left = floor(missing / 2);
  return fill.repeat(left) + value + fill.repeat(missing - left);
}

// Broken into lines no wider than `columns`, at spaces where there is
// one. A word longer than the whole width is broken rather than allowed
// to overhang, because there is nowhere for it to go.
//
// Existing newlines are kept: a paragraph the caller has already broken
// stays broken there.
fun wrap(value, columns) {
  if (columns <= 0) { return [value]; }
  const lines = [];
  for (let paragraph in value.split("\n")) {
    if (paragraph == "") {
      lines.push("");
      continue;
    }
    let current = "";
    for (let word in paragraph.split(" ")) {
      if (word == "") { continue; }
      let candidate = word;
      if (current != "") { candidate = current + " " + word; }
      if (width(candidate) <= columns) {
        current = candidate;
        continue;
      }
      if (current != "") {
        lines.push(current);
        current = "";
      }
      // The word alone may still be too long. Break it at the width
      // rather than leaving it to run off the edge.
      let rest = word;
      while (width(rest) > columns) {
        const head = truncate(rest, columns);
        lines.push(head);
        rest = rest.sub(head.len());
      }
      current = rest;
    }
    lines.push(current);
  }
  if (lines.len() == 0) { lines.push(""); }
  return lines;
}

// Every tab replaced by spaces up to the next stop. A terminal moves the
// cursor on a tab, which would leave holes in a frame that is drawn cell
// by cell, so andy never sends one.
fun expand_tabs(value, stop = 8) {
  if (!value.contains("\t")) { return value; }
  let out = "";
  let column = 0;
  for (let character in value.chars()) {
    if (character == "\t") {
      const advance = stop - column % stop;
      out += " ".repeat(advance);
      column += advance;
      continue;
    }
    out += character;
    column += code_width(character.code_points()[0]);
  }
  return out;
}

// Control characters replaced by something visible, so that a stray byte
// in a file being displayed cannot move the cursor or change the colour.
// Anything andy is asked to draw goes through this.
fun sanitize(value) {
  let safe = true;
  for (let code in value.bytes()) {
    if (code < 32 or code == 0x7f) {
      safe = false;
      break;
    }
  }
  if (safe) { return value; }
  let out = "";
  for (let character in expand_tabs(value).chars()) {
    const code = character.code_points()[0];
    if (code < 32 or code == 0x7f) {
      out += "·";
      continue;
    }
    out += character;
  }
  return out;
}
