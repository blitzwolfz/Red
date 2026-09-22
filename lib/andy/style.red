// How a cell looks: two colours and a handful of attributes.
//
//   import "andy/style" as style;
//
//   const heading = style.Style(color.WHITE, color.BLUE, style.BOLD);
//
// A Style is treated as a value and never changed in place. Everything
// that would change one returns a new one instead, so a style kept in a
// theme cannot be edited from under the widget that is using it, and two
// cells that share a style really do look the same.
//
// That matters for more than tidiness: the terminal backend compares
// styles to decide when to send an escape sequence, and a style that can
// change behind its back would make those comparisons lies.

import "andy/color" as color;

// The attributes, as bits, so that a cell carries them in one number.
const NORMAL = 0;
const BOLD = 1;
const DIM = 2;
const ITALIC = 4;
const UNDERLINE = 8;
const BLINK = 16;
const REVERSE = 32;
const STRIKE = 64;

// A foreground, a background and some attributes.
class Style {
  init(fg = nil, bg = nil, attrs = 0) {
    if (fg == nil) { fg = color.DEFAULT; }
    if (bg == nil) { bg = color.DEFAULT; }
    this.fg = fg;
    this.bg = bg;
    this.attrs = attrs;
  }

  with_fg(value) { return Style(value, this.bg, this.attrs); }
  with_bg(value) { return Style(this.fg, value, this.attrs); }
  with_attrs(value) { return Style(this.fg, this.bg, value); }

  // The same style with some attributes added or taken away.
  plus(attrs) { return Style(this.fg, this.bg, this.attrs | attrs); }
  minus(attrs) { return Style(this.fg, this.bg, this.attrs & ~attrs); }

  has(attr) { return this.attrs & attr != 0; }

  // `other` laid over this one: any colour it names wins, and their
  // attributes are added together. This is how a widget's own style is
  // combined with the one its container asked for, without either having
  // to know what the other chose.
  merge(other) {
    let fg = this.fg;
    let bg = this.bg;
    if (!other.fg.is_default()) { fg = other.fg; }
    if (!other.bg.is_default()) { bg = other.bg; }
    return Style(fg, bg, this.attrs | other.attrs);
  }

  // The two colours swapped. What a selection looks like when there is
  // no colour to spare, and what REVERSE means written out, for a
  // backend that would rather resolve it than send the attribute.
  inverted() { return Style(this.bg, this.fg, this.attrs); }

  // Moved part of the way towards `other`. A disabled control is its own
  // style faded towards the background.
  faded(background, amount = 0.45) {
    return Style(this.fg.mix(background, amount), this.bg, this.attrs);
  }

  str() { return "Style(${this.fg}, ${this.bg}, ${this.attrs})"; }

  eq(other) {
    if (type(other) != "instance" or !(other is Style)) { return false; }
    return this.attrs == other.attrs and this.fg == other.fg and
           this.bg == other.bg;
  }
}

// Whatever the terminal was already doing.
const PLAIN = Style();

// The characters a border is drawn with, in the order the drawing code
// wants them: the four corners, then the two lines.
//
// A border is a set rather than a flag because terminals and fonts
// disagree about which of these they can draw. ASCII is there for a
// terminal that can draw none of them, and is also what the tests
// compare against, since a failure then reads as text rather than as a
// puzzle.
class Border {
  init(name, top_left, top_right, bottom_left, bottom_right,
       horizontal, vertical, tee_left = nil, tee_right = nil,
       tee_top = nil, tee_bottom = nil, cross = nil) {
    this.name = name;
    this.top_left = top_left;
    this.top_right = top_right;
    this.bottom_left = bottom_left;
    this.bottom_right = bottom_right;
    this.horizontal = horizontal;
    this.vertical = vertical;
    // Where lines meet. A table needs all five; a plain box needs none,
    // so they fall back to the corners rather than being required.
    if (tee_left == nil) { tee_left = vertical; }
    if (tee_right == nil) { tee_right = vertical; }
    if (tee_top == nil) { tee_top = horizontal; }
    if (tee_bottom == nil) { tee_bottom = horizontal; }
    if (cross == nil) { cross = horizontal; }
    this.tee_left = tee_left;
    this.tee_right = tee_right;
    this.tee_top = tee_top;
    this.tee_bottom = tee_bottom;
    this.cross = cross;
    this.str_name = name;
  }

  str() { return "Border(${this.name})"; }
}

const SINGLE = Border("single", "┌", "┐", "└", "┘", "─", "│",
                      "├", "┤", "┬", "┴", "┼");
const ROUNDED = Border("rounded", "╭", "╮", "╰", "╯", "─", "│",
                       "├", "┤", "┬", "┴", "┼");
const DOUBLE = Border("double", "╔", "╗", "╚", "╝", "═", "║",
                      "╠", "╣", "╦", "╩", "╬");
const THICK = Border("thick", "┏", "┓", "┗", "┛", "━", "┃",
                     "┣", "┫", "┳", "┻", "╋");
const ASCII = Border("ascii", "+", "+", "+", "+", "-", "|",
                     "+", "+", "+", "+", "+");
// Solid blocks, for a frame that reads as a filled shape rather than as
// a line drawing.
const BLOCK = Border("block", "█", "█", "█", "█", "▀", "█",
                     "█", "█", "█", "█", "█");

const BORDERS = {
  "single": SINGLE,
  "rounded": ROUNDED,
  "double": DOUBLE,
  "thick": THICK,
  "ascii": ASCII,
  "block": BLOCK,
};

// A border by name, falling back to a single line. A name that is not
// one of these is a typo in a program rather than a value from outside,
// so it falls back quietly instead of raising: a wrong looking box is
// easier to see than an error is to read.
fun border(name) { return BORDERS.get(name, SINGLE); }

// The eight steps between an empty cell and a full one, used for bars
// and meters that want more resolution than one cell gives.
const BLOCKS_HORIZONTAL = ["", "▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"];
const BLOCKS_VERTICAL = ["", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"];

// The shades, for a background that has to be visibly different without
// using a colour.
const SHADES = ["░", "▒", "▓", "█"];
