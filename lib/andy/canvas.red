// The surface everything is drawn on: a grid of cells.
//
//   import "andy/canvas" as canvas;
//
//   const surface = canvas.Canvas(80, 24);
//   surface.box(geom.Rect(0, 0, 20, 3), style.SINGLE, theme.border);
//   surface.text(2, 1, "hello", theme.text);
//
// A cell holds one thing to draw and one style. A widget draws into the
// canvas and never talks to a terminal or a window, which is what makes
// a widget the same widget on both backends, and what makes it possible
// to draw a whole interface into a string and compare it in a test.
//
// Two ideas carry most of the weight.
//
// **Frames.** A widget is given a rectangle and draws at 0, 0. The canvas
// keeps a stack of origins and clips, so a widget's coordinates are its
// own and it cannot draw outside what it was given, however wrong its
// arithmetic is. That is the difference between a bug that misplaces a
// label and a bug that corrupts the whole screen.
//
// **Wide characters.** A character two columns wide occupies two cells:
// the first holds it, the second holds nothing and is skipped. Writing
// over either one clears both, because half of a character is not a
// character.

import "andy/geom" as geom;
import "andy/style" as style;
import "andy/text" as text;
import "andy/color" as color;

const Rect = geom.Rect;
const Style = style.Style;

// The marker left in the cell after a double width character. It is not
// the empty string, which is what a cleared cell holds, because the two
// have to be told apart: one is the right half of something, the other
// is nothing at all.
const CONTINUATION = nil;

class Canvas {
  init(width, height, fill_style = nil) {
    if (fill_style == nil) { fill_style = style.PLAIN; }
    this.width = max(0, width);
    this.height = max(0, height);
    this.base = fill_style;
    this.chars = [];
    this.styles = [];
    // Where the caret should sit, or nil for no caret. A text field sets
    // this while it is drawing itself; the backend puts the real cursor
    // there, which is what makes a terminal's own caret land in the
    // right place and an input method attach to it.
    this.cursor = nil;
    // The stack of frames. Each entry is [origin x, origin y, clip], and
    // the clip is in absolute coordinates so that testing a point costs
    // no arithmetic.
    this.stack = [];
    this.origin_x = 0;
    this.origin_y = 0;
    this.clip = Rect(0, 0, this.width, this.height);
    this.reset();
  }

  // Throws away everything drawn and starts again from the base style.
  reset() {
    const count = this.width * this.height;
    this.chars = [];
    this.styles = [];
    for (let i in range(0, count)) {
      this.chars.push(" ");
      this.styles.push(this.base);
    }
    this.cursor = nil;
    this.stack = [];
    this.origin_x = 0;
    this.origin_y = 0;
    this.clip = Rect(0, 0, this.width, this.height);
    return this;
  }

  // A new size, and a blank surface. Nothing is carried across: a resize
  // is followed by a redraw, always, because a layout computed for one
  // size says nothing about another.
  resize(width, height) {
    this.width = max(0, width);
    this.height = max(0, height);
    return this.reset();
  }

  size() { return geom.Size(this.width, this.height); }

  // The whole surface, in absolute coordinates.
  bounds() { return Rect(0, 0, this.width, this.height); }

  // The rectangle a widget is currently allowed to draw in, in its own
  // coordinates. A widget that wants to skip work it cannot see asks for
  // this rather than assuming it can see all of itself.
  visible() {
    return Rect(this.clip.x - this.origin_x, this.clip.y - this.origin_y,
                this.clip.width, this.clip.height);
  }

  // ---- frames ----

  // Draw inside `rect`, in coordinates starting at 0, 0, with everything
  // outside it clipped away. The rectangle is given in the coordinates
  // currently in force, so frames nest the way containers do.
  //
  // Always pair this with pop(). `frame()` below does that for you and
  // is what widget code actually calls.
  push(rect) {
    this.stack.push([this.origin_x, this.origin_y, this.clip]);
    const absolute = Rect(this.origin_x + rect.x, this.origin_y + rect.y,
                          rect.width, rect.height);
    this.origin_x = absolute.x;
    this.origin_y = absolute.y;
    // A child can only ever be given less than its parent had, never
    // more, so the new clip is the overlap rather than the request.
    this.clip = this.clip.intersect(absolute);
    return this;
  }

  pop() {
    if (this.stack.len() == 0) { return this; }
    const [x, y, clip] = this.stack.pop();
    this.origin_x = x;
    this.origin_y = y;
    this.clip = clip;
    return this;
  }

  // Runs `body` with `rect` as the frame, and restores what was there
  // however the body ends. Every container uses this to draw a child.
  frame(rect, body) {
    this.push(rect);
    try {
      return body();
    } finally {
      this.pop();
    }
  }

  // Narrows the clip without moving the origin. What a scrolling view
  // does: its children keep their coordinates and are cut off at the
  // edges of the window they are seen through.
  clipped(rect, body) {
    this.stack.push([this.origin_x, this.origin_y, this.clip]);
    const absolute = Rect(this.origin_x + rect.x, this.origin_y + rect.y,
                          rect.width, rect.height);
    this.clip = this.clip.intersect(absolute);
    try {
      return body();
    } finally {
      this.pop();
    }
  }

  // ---- single cells ----

  // Is this point, in the current frame, somewhere we may draw?
  can_draw(x, y) {
    return this.clip.contains(this.origin_x + x, this.origin_y + y);
  }

  // Writes one thing at one position. `cluster` is a character, possibly
  // with combining marks attached; `advance` is how many columns it
  // takes, which the caller has usually already measured.
  //
  // Nothing outside the clip is written, and nothing outside the canvas:
  // the two checks are separate because a clip can be larger than the
  // surface when a widget is placed partly off screen.
  set(x, y, cluster, cell_style = nil, advance = nil) {
    if (cell_style == nil) { cell_style = this.base; }
    const ax = this.origin_x + x;
    const ay = this.origin_y + y;
    if (!this.clip.contains(ax, ay)) { return this; }
    if (ax < 0 or ay < 0 or ax >= this.width or ay >= this.height) {
      return this;
    }
    if (advance == nil) { advance = text.width(cluster); }
    const index = ay * this.width + ax;

    // If we are landing on the right half of a wide character, the left
    // half must go too, or the terminal is left holding half of one.
    this.break_pair(ax, ay);

    this.chars[index] = cluster;
    this.styles[index] = cell_style;
    if (advance >= 2) {
      const rx = ax + 1;
      if (rx < this.width and this.clip.contains(rx, ay)) {
        this.break_pair(rx, ay);
        this.chars[ay * this.width + rx] = CONTINUATION;
        this.styles[ay * this.width + rx] = cell_style;
      } else {
        // The second half will not fit. Draw a space rather than a
        // character the terminal would have to cut in two.
        this.chars[index] = " ";
      }
    }
    return this;
  }

  // Clears whichever half of a wide character this cell belongs to, so
  // that a partial overwrite never leaves an orphan. Absolute
  // coordinates: this is below the clip, not subject to it.
  break_pair(ax, ay) {
    const index = ay * this.width + ax;
    if (this.chars[index] == CONTINUATION) {
      if (ax > 0) { this.chars[index - 1] = " "; }
      this.chars[index] = " ";
      return this;
    }
    if (ax + 1 < this.width and
        this.chars[index + 1] == CONTINUATION) {
      this.chars[index + 1] = " ";
    }
    return this;
  }

  // What is at a position, as [cluster, style], or nil when the position
  // is not on the canvas. Tests read the screen with this; so does the
  // shadow below, which has to know what it is dimming.
  at(x, y) {
    const ax = this.origin_x + x;
    const ay = this.origin_y + y;
    if (ax < 0 or ay < 0 or ax >= this.width or ay >= this.height) {
      return nil;
    }
    const index = ay * this.width + ax;
    return [this.chars[index], this.styles[index]];
  }

  // ---- runs and shapes ----

  // Draws a string starting at x, y, and gives back how many columns it
  // took. Control characters are replaced rather than sent, because a
  // terminal acts on them and a canvas cannot.
  //
  // `limit` stops the string early, in columns. Without it the string
  // runs to the edge of the clip, which is usually what a label wants.
  text(x, y, value, cell_style = nil, limit = nil) {
    if (cell_style == nil) { cell_style = this.base; }
    let column = x;
    let used = 0;
    for (let [cluster, advance] in text.clusters(text.sanitize(value))) {
      if (limit != nil and used + advance > limit) { break; }
      this.set(column, y, cluster, cell_style, advance);
      column += advance;
      used += advance;
    }
    return used;
  }

  // The same, but the string is cut with an ellipsis when it does not
  // fit. A label in a column too narrow for it should say so, rather
  // than stop mid word and look like the data is wrong.
  text_ellipsized(x, y, value, cell_style = nil, limit = nil) {
    if (limit == nil) { limit = this.visible().right() - x; }
    return this.text(x, y, text.ellipsize(value, limit), cell_style, limit);
  }

  // Every cell of `rect` set to one character. With no character it
  // fills with spaces, which is how a panel paints its background.
  fill(rect, cluster = " ", cell_style = nil) {
    const advance = text.width(cluster);
    for (let y in range(rect.y, rect.bottom())) {
      for (let x in range(rect.x, rect.right())) {
        this.set(x, y, cluster, cell_style, advance);
      }
    }
    return this;
  }

  // The style of every cell in `rect` replaced, leaving what is drawn
  // there alone. Selection, hover and disabling are all this: the text
  // is already right, only its colours change.
  restyle(rect, cell_style) {
    for (let y in range(rect.y, rect.bottom())) {
      for (let x in range(rect.x, rect.right())) {
        const ax = this.origin_x + x;
        const ay = this.origin_y + y;
        if (!this.clip.contains(ax, ay)) { continue; }
        if (ax < 0 or ay < 0 or ax >= this.width or ay >= this.height) {
          continue;
        }
        this.styles[ay * this.width + ax] = cell_style;
      }
    }
    return this;
  }

  // The style of every cell in `rect` put through `change`, which is
  // given the style and returns a new one. Dimming what is behind a
  // dialog is written this way.
  map_style(rect, change) {
    for (let y in range(rect.y, rect.bottom())) {
      for (let x in range(rect.x, rect.right())) {
        const ax = this.origin_x + x;
        const ay = this.origin_y + y;
        if (!this.clip.contains(ax, ay)) { continue; }
        if (ax < 0 or ay < 0 or ax >= this.width or ay >= this.height) {
          continue;
        }
        const index = ay * this.width + ax;
        this.styles[index] = change(this.styles[index]);
      }
    }
    return this;
  }

  hline(x, y, length, cluster = "─", cell_style = nil) {
    for (let i in range(0, max(0, length))) {
      this.set(x + i, y, cluster, cell_style, 1);
    }
    return this;
  }

  vline(x, y, length, cluster = "│", cell_style = nil) {
    for (let i in range(0, max(0, length))) {
      this.set(x, y + i, cluster, cell_style, 1);
    }
    return this;
  }

  // A rectangle outlined with one of the border sets. A rectangle one
  // cell wide or tall degenerates to a line, which is what a caller
  // laying out a shrinking pane wants rather than an error.
  box(rect, border = nil, cell_style = nil) {
    if (border == nil) { border = style.SINGLE; }
    if (rect.is_empty()) { return this; }
    const right = rect.right() - 1;
    const bottom = rect.bottom() - 1;

    if (rect.height == 1) {
      this.hline(rect.x, rect.y, rect.width, border.horizontal, cell_style);
      return this;
    }
    if (rect.width == 1) {
      this.vline(rect.x, rect.y, rect.height, border.vertical, cell_style);
      return this;
    }

    this.hline(rect.x + 1, rect.y, rect.width - 2, border.horizontal, cell_style);
    this.hline(rect.x + 1, bottom, rect.width - 2, border.horizontal, cell_style);
    this.vline(rect.x, rect.y + 1, rect.height - 2, border.vertical, cell_style);
    this.vline(right, rect.y + 1, rect.height - 2, border.vertical, cell_style);
    this.set(rect.x, rect.y, border.top_left, cell_style, 1);
    this.set(right, rect.y, border.top_right, cell_style, 1);
    this.set(rect.x, bottom, border.bottom_left, cell_style, 1);
    this.set(right, bottom, border.bottom_right, cell_style, 1);
    return this;
  }

  // A title written into the top edge of a box, with a space either side
  // so that it does not touch the corners. The title is cut to what is
  // left after those spaces; `align` is "left", "center" or "right".
  box_title(rect, title, cell_style = nil, align = "left") {
    if (title == "" or rect.width <= 4) { return this; }
    const room = rect.width - 4;
    const label = " " + text.ellipsize(title, room) + " ";
    const labelWidth = text.width(label);
    let x = rect.x + 2;
    if (align == "center") {
      x = rect.x + floor((rect.width - labelWidth) / 2);
    } else if (align == "right") {
      x = rect.right() - 2 - labelWidth;
    }
    this.text(x, rect.y, label, cell_style);
    return this;
  }

  // A box with a title in one call, which is what a framed panel wants.
  titled_box(rect, title, border = nil, cell_style = nil,
             title_style = nil, align = "left") {
    this.box(rect, border, cell_style);
    if (title_style == nil) { title_style = cell_style; }
    this.box_title(rect, title, title_style, align);
    return this;
  }

  // A soft drop shadow to the bottom right of a rectangle: the cells
  // there keep what they are showing and have their colours pulled
  // towards black. Used under dialogs, where it is the cheapest way to
  // say "this is in front".
  shadow(rect, amount = 0.6) {
    const right = Rect(rect.right(), rect.y + 1, 1, max(0, rect.height - 1));
    const bottom = Rect(rect.x + 1, rect.bottom(), rect.width, 1);
    const darken = fun (existing) {
      return Style(existing.fg.mix(color.BLACK, amount),
                   existing.bg.mix(color.BLACK, amount),
                   existing.attrs);
    };
    this.map_style(right, darken);
    this.map_style(bottom, darken);
    return this;
  }

  // ---- the caret ----

  // Put the caret here, in the current frame's coordinates. A widget
  // calls this while drawing itself, so the caret follows whatever was
  // drawn last and focused, with no separate bookkeeping.
  place_cursor(x, y) {
    const ax = this.origin_x + x;
    const ay = this.origin_y + y;
    if (!this.clip.contains(ax, ay)) { return this; }
    this.cursor = geom.Point(ax, ay);
    return this;
  }

  hide_cursor() {
    this.cursor = nil;
    return this;
  }

  // ---- compositing and comparison ----

  // Copies `other` onto this canvas with its top left at x, y. Layers —
  // a dialog over a window, a menu over a bar — are drawn on their own
  // canvas and then put down with this, which keeps a layer's own
  // coordinates simple and lets it be measured before it is placed.
  blit(other, x, y) {
    for (let sy in range(0, other.height)) {
      for (let sx in range(0, other.width)) {
        const index = sy * other.width + sx;
        const cluster = other.chars[index];
        if (cluster == CONTINUATION) { continue; }
        let advance = 1;
        if (cluster != " ") { advance = text.width(cluster); }
        this.set(x + sx, y + sy, cluster, other.styles[index], advance);
      }
    }
    if (other.cursor != nil) {
      this.place_cursor(x + other.cursor.x, y + other.cursor.y);
    }
    return this;
  }

  // One row as a string, with a continuation cell contributing nothing
  // because the character before it already covered that column.
  row(y) {
    if (y < 0 or y >= this.height) { return ""; }
    const parts = [];
    for (let x in range(0, this.width)) {
      const cluster = this.chars[y * this.width + x];
      if (cluster == CONTINUATION) { continue; }
      parts.push(cluster);
    }
    return parts.join("");
  }

  // The whole surface as lines of text, with the colours dropped. This
  // is how andy is tested: an interface is drawn, turned into strings,
  // and compared with what it should look like, which is a test somebody
  // can read.
  lines() {
    const out = [];
    for (let y in range(0, this.height)) { out.push(this.row(y)); }
    return out;
  }

  str() { return this.lines().join("\n"); }

  // The cells that differ from `previous`, as runs of neighbouring cells
  // sharing a style. A backend redraws these and nothing else, which is
  // the difference between a terminal that flickers and one that does
  // not.
  //
  // Each run is [x, y, style, clusters] where clusters is an array. A
  // canvas of a different size has nothing in common with this one, so
  // everything is a difference.
  diff(previous) {
    const runs = [];
    const sizeChanged = previous == nil or
                        previous.width != this.width or
                        previous.height != this.height;
    for (let y in range(0, this.height)) {
      let runX = -1;
      let runStyle = nil;
      let runCells = [];
      for (let x in range(0, this.width)) {
        const index = y * this.width + x;
        const cluster = this.chars[index];
        const cellStyle = this.styles[index];
        let changed = sizeChanged;
        if (!changed) {
          changed = previous.chars[index] != cluster or
                    !(previous.styles[index] == cellStyle);
        }
        // A continuation cell is not drawn on its own; it travels with
        // the character to its left, which is already in the run.
        if (cluster == CONTINUATION) {
          if (changed and runX < 0) {
            // The left half did not change but this one did, which means
            // something scrolled underneath. Redraw from the left half.
            runX = x - 1;
            runStyle = this.styles[index - 1];
            runCells = [this.chars[index - 1]];
          }
          continue;
        }
        const fits = changed and runX >= 0 and runStyle == cellStyle;
        if (fits) {
          runCells.push(cluster);
          continue;
        }
        if (runX >= 0) {
          runs.push([runX, y, runStyle, runCells]);
          runX = -1;
          runCells = [];
        }
        if (changed) {
          runX = x;
          runStyle = cellStyle;
          runCells = [cluster];
        }
      }
      if (runX >= 0) { runs.push([runX, y, runStyle, runCells]); }
    }
    return runs;
  }

  // A copy, for a backend to keep as "what is on the screen now".
  snapshot() {
    const copy = Canvas(this.width, this.height, this.base);
    for (let i in range(0, this.chars.len())) {
      copy.chars[i] = this.chars[i];
      copy.styles[i] = this.styles[i];
    }
    copy.cursor = this.cursor;
    return copy;
  }
}
