// Points, sizes and rectangles.
//
//   import "andy/geom" as geom;
//
// Everything andy measures is measured in cells: one character wide and
// one line tall. That is the unit the terminal has, and the native
// backend draws on the same grid so that a program written against one
// looks like itself on the other.
//
// A rectangle is a corner and a size, and its edges are half open: a
// rectangle at x 0 with width 3 covers columns 0, 1 and 2. Every
// operation here keeps that, which is what lets rectangles be sliced and
// laid end to end without a fence post error waiting in the gap.

// A position on the grid.
class Point {
  init(x = 0, y = 0) {
    this.x = x;
    this.y = y;
  }

  moved(dx, dy) { return Point(this.x + dx, this.y + dy); }

  str() { return "(${this.x}, ${this.y})"; }

  eq(other) {
    return type(other) == "instance" and other is Point and
           this.x == other.x and this.y == other.y;
  }
}

// How wide and how tall, in cells. Never negative: a size given a
// negative number is clamped to zero, because a widget that asks for
// minus three columns means it wants none.
class Size {
  init(width = 0, height = 0) {
    this.width = max(0, width);
    this.height = max(0, height);
  }

  is_empty() { return this.width <= 0 or this.height <= 0; }

  // The larger of the two in each direction. What a container does when
  // it has to hold several things at once.
  union(other) {
    return Size(max(this.width, other.width), max(this.height, other.height));
  }

  grown(width, height) { return Size(this.width + width, this.height + height); }

  str() { return "${this.width}x${this.height}"; }

  eq(other) {
    return type(other) == "instance" and other is Size and
           this.width == other.width and this.height == other.height;
  }
}

// A region of the grid: a corner, a width and a height.
class Rect {
  init(x = 0, y = 0, width = 0, height = 0) {
    this.x = x;
    this.y = y;
    this.width = max(0, width);
    this.height = max(0, height);
  }

  // The column after the last one covered, and the row after the last.
  // Half open, so `right` is where the next rectangle begins.
  right() { return this.x + this.width; }
  bottom() { return this.y + this.height; }

  is_empty() { return this.width <= 0 or this.height <= 0; }

  size() { return Size(this.width, this.height); }
  origin() { return Point(this.x, this.y); }

  contains(x, y) {
    return x >= this.x and x < this.right() and
           y >= this.y and y < this.bottom();
  }

  contains_rect(other) {
    if (other.is_empty()) { return true; }
    return other.x >= this.x and other.y >= this.y and
           other.right() <= this.right() and other.bottom() <= this.bottom();
  }

  moved(dx, dy) {
    return Rect(this.x + dx, this.y + dy, this.width, this.height);
  }

  // The same rectangle with its corner put somewhere else.
  at(x, y) { return Rect(x, y, this.width, this.height); }

  resized(width, height) { return Rect(this.x, this.y, width, height); }

  // Pulled in by `insets` on every side. This is how a frame hands its
  // contents the space inside its border, and how padding is applied.
  // Shrinking past nothing gives an empty rectangle in the right place
  // rather than an inside out one.
  shrunk(insets) {
    const x = this.x + insets.left;
    const y = this.y + insets.top;
    const width = this.width - insets.left - insets.right;
    const height = this.height - insets.top - insets.bottom;
    return Rect(x, y, max(0, width), max(0, height));
  }

  grown(insets) {
    return Rect(this.x - insets.left, this.y - insets.top,
                this.width + insets.left + insets.right,
                this.height + insets.top + insets.bottom);
  }

  // The overlap of two rectangles, which is empty when they do not
  // touch. Clipping is this and nothing else.
  intersect(other) {
    const x = max(this.x, other.x);
    const y = max(this.y, other.y);
    const right = min(this.right(), other.right());
    const bottom = min(this.bottom(), other.bottom());
    if (right <= x or bottom <= y) { return Rect(x, y, 0, 0); }
    return Rect(x, y, right - x, bottom - y);
  }

  // The smallest rectangle holding both. An empty operand contributes
  // nothing, so folding this over a list of children ignores the ones
  // that asked for no room.
  union(other) {
    if (this.is_empty()) { return other; }
    if (other.is_empty()) { return this; }
    const x = min(this.x, other.x);
    const y = min(this.y, other.y);
    const right = max(this.right(), other.right());
    const bottom = max(this.bottom(), other.bottom());
    return Rect(x, y, right - x, bottom - y);
  }

  overlaps(other) { return !this.intersect(other).is_empty(); }

  // Split off `amount` cells from one edge, and give back the piece and
  // what is left. Laying a status bar along the bottom, or a sidebar
  // down one side, is one call and no arithmetic at the call site.
  //
  //   const [bar, rest] = area.split_top(1);
  split_top(amount) {
    const cut = min(max(0, amount), this.height);
    return [Rect(this.x, this.y, this.width, cut),
            Rect(this.x, this.y + cut, this.width, this.height - cut)];
  }

  split_bottom(amount) {
    const cut = min(max(0, amount), this.height);
    return [Rect(this.x, this.bottom() - cut, this.width, cut),
            Rect(this.x, this.y, this.width, this.height - cut)];
  }

  split_left(amount) {
    const cut = min(max(0, amount), this.width);
    return [Rect(this.x, this.y, cut, this.height),
            Rect(this.x + cut, this.y, this.width - cut, this.height)];
  }

  split_right(amount) {
    const cut = min(max(0, amount), this.width);
    return [Rect(this.right() - cut, this.y, cut, this.height),
            Rect(this.x, this.y, this.width - cut, this.height)];
  }

  // Put a size in the middle of this rectangle. Dialogs.
  center(size) {
    const width = min(size.width, this.width);
    const height = min(size.height, this.height);
    return Rect(this.x + floor((this.width - width) / 2),
                this.y + floor((this.height - height) / 2),
                width, height);
  }

  str() { return "${this.width}x${this.height}+${this.x}+${this.y}"; }

  eq(other) {
    return type(other) == "instance" and other is Rect and
           this.x == other.x and this.y == other.y and
           this.width == other.width and this.height == other.height;
  }
}

// Space around something: a border, a margin, a padding.
class Insets {
  init(top = 0, right = 0, bottom = 0, left = 0) {
    this.top = top;
    this.right = right;
    this.bottom = bottom;
    this.left = left;
  }

  horizontal() { return this.left + this.right; }
  vertical() { return this.top + this.bottom; }

  plus(other) {
    return Insets(this.top + other.top, this.right + other.right,
                  this.bottom + other.bottom, this.left + other.left);
  }

  str() {
    return "Insets(${this.top}, ${this.right}, ${this.bottom}, ${this.left})";
  }
}

// The same on all four sides, which is what padding usually is.
fun uniform(amount) { return Insets(amount, amount, amount, amount); }

// A different amount up and down from side to side. Text wants more room
// horizontally than vertically, because a cell is taller than it is
// wide, so this is the common one.
fun symmetric(vertical, horizontal) {
  return Insets(vertical, horizontal, vertical, horizontal);
}

const NONE = Insets(0, 0, 0, 0);

// Pins a number into a range. Used everywhere a scroll offset or a
// selection index has to stay inside something.
fun clamp(value, low, high) {
  if (value < low) { return low; }
  if (value > high) { return high; }
  return value;
}
