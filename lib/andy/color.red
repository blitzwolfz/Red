// Colour, and what to do when the screen has less of it than you asked
// for.
//
//   import "andy/color" as color;
//
//   const warning = color.rgb(0xff, 0xb0, 0x30);
//   const plain = color.DEFAULT;
//
// A colour is written once, in full, as red green and blue. What reaches
// the screen depends on the screen: a modern terminal is told the exact
// value, an older one is given the nearest of 256, a very old one the
// nearest of 16, and a monochrome one nothing at all. A program says what
// it means and the backend does the arithmetic.
//
// DEFAULT is not a colour. It means "whatever the terminal was already
// using", which is the only way to sit properly inside a colour scheme
// somebody else chose.

// How much colour a screen can show. The terminal backend works this out
// from the environment once and then stops asking.
enum Depth { None, Ansi16, Indexed256, TrueColor }

// One colour, or the absence of a choice.
//
// `kind` is "default", "rgb", "ansi" or "indexed". The first carries no
// numbers; the second carries red, green and blue; the last two carry an
// index into a palette the terminal owns, which is how a program asks for
// "the user's idea of red" rather than for a particular red.
class Color {
  init(kind, r = 0, g = 0, b = 0, index = 0) {
    this.kind = kind;
    this.r = r;
    this.g = g;
    this.b = b;
    this.index = index;
  }

  is_default() { return this.kind == "default"; }

  // The colour as one number, 0xRRGGBB. An indexed colour is resolved
  // against the standard palette first, so that anything can be measured
  // against anything; a default colour has no value and gives nil.
  packed() {
    if (this.kind == "default") { return nil; }
    if (this.kind == "rgb") { return this.r * 65536 + this.g * 256 + this.b; }
    const [r, g, b] = palette_rgb(this.index);
    return r * 65536 + g * 256 + b;
  }

  // The red, green and blue of this colour, resolving a palette index.
  // Nil for the default colour.
  parts() {
    if (this.kind == "default") { return nil; }
    if (this.kind == "rgb") { return [this.r, this.g, this.b]; }
    return palette_rgb(this.index);
  }

  // A colour between this one and `other`, `amount` of the way across.
  // Used for a disabled control, a dimmed border, a selection that has
  // lost focus: all of them are a colour moved part of the way towards
  // another one.
  mix(other, amount) {
    const here = this.parts();
    const there = other.parts();
    if (here == nil or there == nil) { return this; }
    const t = clamp01(amount);
    return rgb(round(here[0] + (there[0] - here[0]) * t),
               round(here[1] + (there[1] - here[1]) * t),
               round(here[2] + (there[2] - here[2]) * t));
  }

  // Perceived brightness, 0 to 1. The weights are the usual ones: the
  // eye is far more sensitive to green than to blue, so an even average
  // would call yellow and blue equally bright, and they are not.
  luminance() {
    const parts = this.parts();
    if (parts == nil) { return 0.5; }
    return (parts[0] * 0.299 + parts[1] * 0.587 + parts[2] * 0.114) / 255;
  }

  // Black or white, whichever can be read on top of this colour. What a
  // widget uses when it is given a background and has to find its own
  // foreground.
  contrasting() {
    if (this.luminance() > 0.55) { return BLACK; }
    return WHITE;
  }

  str() {
    if (this.kind == "default") { return "default"; }
    if (this.kind == "rgb") { return "#" + hex2(this.r) + hex2(this.g) + hex2(this.b); }
    return "${this.kind}(${this.index})";
  }

  eq(other) {
    if (type(other) != "instance" or !(other is Color)) { return false; }
    if (this.kind != other.kind) { return false; }
    if (this.kind == "default") { return true; }
    if (this.kind == "rgb") {
      return this.r == other.r and this.g == other.g and this.b == other.b;
    }
    return this.index == other.index;
  }
}

fun clamp01(value) {
  if (value < 0) { return 0; }
  if (value > 1) { return 1; }
  return value;
}

fun byte(value) {
  const n = round(value);
  if (n < 0) { return 0; }
  if (n > 255) { return 255; }
  return n;
}

fun hex2(value) {
  const digits = "0123456789abcdef";
  const n = byte(value);
  return digits[floor(n / 16)] + digits[n % 16];
}

// A colour from three components, each 0 to 255.
fun rgb(r, g, b) { return Color("rgb", byte(r), byte(g), byte(b)); }

// A colour written the way a designer writes one: "#1e6fd9", or "#1e6"
// for the short form, with or without the hash. Nil when it is not one,
// so a colour read from a configuration file can be checked.
fun parse(text) {
  let body = text.trim();
  if (body.starts_with("#")) { body = body.sub(1); }
  if (body.len() == 3) {
    const r = hex_digit(body[0]);
    const g = hex_digit(body[1]);
    const b = hex_digit(body[2]);
    if (r == nil or g == nil or b == nil) { return nil; }
    return rgb(r * 17, g * 17, b * 17);
  }
  if (body.len() != 6) { return nil; }
  let parts = [];
  for (let i in range(0, 3)) {
    const high = hex_digit(body[i * 2]);
    const low = hex_digit(body[i * 2 + 1]);
    if (high == nil or low == nil) { return nil; }
    parts.push(high * 16 + low);
  }
  return rgb(parts[0], parts[1], parts[2]);
}

fun hex_digit(character) {
  const index = "0123456789abcdef".find(character.lower());
  if (index < 0) { return nil; }
  return index;
}

// One of the sixteen colours the terminal itself defines. Prefer these
// for anything that should follow the user's own scheme.
fun ansi(index) { return Color("ansi", 0, 0, 0, floor(index) % 16); }

// One of the 256 colours of the extended palette.
fun indexed(index) { return Color("indexed", 0, 0, 0, floor(index) % 256); }

// Hue in degrees, saturation and lightness from 0 to 1. Generating a
// series of colours that belong together is much easier here than in
// red, green and blue, so a chart or a set of tabs is usually written
// this way and converted once.
fun hsl(hue, saturation, lightness) {
  const h = ((hue % 360) + 360) % 360;
  const s = clamp01(saturation);
  const l = clamp01(lightness);
  const c = (1 - abs(2 * l - 1)) * s;
  const x = c * (1 - abs((h / 60) % 2 - 1));
  const m = l - c / 2;
  let parts = [0, 0, 0];
  if (h < 60) { parts = [c, x, 0]; }
  else if (h < 120) { parts = [x, c, 0]; }
  else if (h < 180) { parts = [0, c, x]; }
  else if (h < 240) { parts = [0, x, c]; }
  else if (h < 300) { parts = [x, 0, c]; }
  else { parts = [c, 0, x]; }
  return rgb((parts[0] + m) * 255, (parts[1] + m) * 255, (parts[2] + m) * 255);
}

// Whatever the terminal was using. Not a colour: a decision not to make
// one.
const DEFAULT = Color("default");

const BLACK = rgb(0, 0, 0);
const WHITE = rgb(255, 255, 255);
const RED = rgb(0xd0, 0x30, 0x30);
const GREEN = rgb(0x2e, 0xa0, 0x43);
const YELLOW = rgb(0xd8, 0xa6, 0x18);
const BLUE = rgb(0x28, 0x6d, 0xd0);
const MAGENTA = rgb(0xa8, 0x48, 0xc0);
const CYAN = rgb(0x21, 0x9a, 0xa8);
const GREY = rgb(0x80, 0x80, 0x80);
const ORANGE = rgb(0xe0, 0x7b, 0x20);

// The sixteen terminal colours, by name, for a program that wants to sit
// inside the user's scheme rather than beside it.
const TERM_BLACK = ansi(0);
const TERM_RED = ansi(1);
const TERM_GREEN = ansi(2);
const TERM_YELLOW = ansi(3);
const TERM_BLUE = ansi(4);
const TERM_MAGENTA = ansi(5);
const TERM_CYAN = ansi(6);
const TERM_WHITE = ansi(7);
const TERM_BRIGHT_BLACK = ansi(8);
const TERM_BRIGHT_RED = ansi(9);
const TERM_BRIGHT_GREEN = ansi(10);
const TERM_BRIGHT_YELLOW = ansi(11);
const TERM_BRIGHT_BLUE = ansi(12);
const TERM_BRIGHT_MAGENTA = ansi(13);
const TERM_BRIGHT_CYAN = ansi(14);
const TERM_BRIGHT_WHITE = ansi(15);

// The first sixteen entries of the xterm palette. Terminals vary here,
// because these are exactly the ones a user is invited to change, so
// these values are only used for measuring distance when a true colour
// has to be reduced to a palette entry.
const BASE_16 = [
  [0x00, 0x00, 0x00], [0x80, 0x00, 0x00], [0x00, 0x80, 0x00], [0x80, 0x80, 0x00],
  [0x00, 0x00, 0x80], [0x80, 0x00, 0x80], [0x00, 0x80, 0x80], [0xc0, 0xc0, 0xc0],
  [0x80, 0x80, 0x80], [0xff, 0x00, 0x00], [0x00, 0xff, 0x00], [0xff, 0xff, 0x00],
  [0x00, 0x00, 0xff], [0xff, 0x00, 0xff], [0x00, 0xff, 0xff], [0xff, 0xff, 0xff],
];

// The six levels the 6x6x6 cube uses. Not evenly spaced: the gap from
// nothing to the first step is larger than the rest, which is what makes
// the dark end of the cube usable.
const CUBE_LEVELS = [0, 95, 135, 175, 215, 255];

// What palette entry `index` stands for, as red, green and blue.
fun palette_rgb(index) {
  const n = floor(index) % 256;
  if (n < 16) { return BASE_16[n]; }
  if (n < 232) {
    const offset = n - 16;
    return [CUBE_LEVELS[floor(offset / 36)],
            CUBE_LEVELS[floor(offset / 6) % 6],
            CUBE_LEVELS[offset % 6]];
  }
  // The last 24 entries are a ramp of greys between black and white,
  // which is why text can be dimmed on a 256 colour terminal at all.
  const level = 8 + (n - 232) * 10;
  return [level, level, level];
}

// Which of the 256 palette entries is closest to a colour. Distance is
// measured on the straight line through red, green and blue: it is not
// how the eye works, but it is what every terminal library does, and the
// palette is coarse enough that a better metric changes almost nothing.
fun nearest_256(c) {
  const parts = c.parts();
  if (parts == nil) { return 0; }
  const [r, g, b] = parts;

  // The cube is regular, so the nearest entry in it can be worked out
  // rather than searched for.
  const ci = cube_level(r) * 36 + cube_level(g) * 6 + cube_level(b) + 16;
  let best = ci;
  let bestDistance = distance(parts, palette_rgb(ci));

  // The grey ramp is finer than the cube's own greys, so a nearly grey
  // colour usually lands there instead.
  let grey = round((r + g + b) / 3);
  let step = round((grey - 8) / 10);
  if (step < 0) { step = 0; }
  if (step > 23) { step = 23; }
  const gi = 232 + step;
  const greyDistance = distance(parts, palette_rgb(gi));
  if (greyDistance < bestDistance) {
    best = gi;
    bestDistance = greyDistance;
  }
  return best;
}

fun cube_level(value) {
  let best = 0;
  let bestDistance = 1000000;
  for (let i in range(0, 6)) {
    const d = abs(CUBE_LEVELS[i] - value);
    if (d < bestDistance) {
      bestDistance = d;
      best = i;
    }
  }
  return best;
}

// Which of the sixteen terminal colours is closest.
fun nearest_16(c) {
  const parts = c.parts();
  if (parts == nil) { return 7; }
  let best = 0;
  let bestDistance = 100000000;
  for (let i in range(0, 16)) {
    const d = distance(parts, BASE_16[i]);
    if (d < bestDistance) {
      bestDistance = d;
      best = i;
    }
  }
  return best;
}

fun distance(a, b) {
  const dr = a[0] - b[0];
  const dg = a[1] - b[1];
  const db = a[2] - b[2];
  return dr * dr + dg * dg + db * db;
}
