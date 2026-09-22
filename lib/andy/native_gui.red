// Pixel primitives for native windows.
//
// This module is intentionally separate from andy/canvas. Canvas is the
// terminal-compatible cell surface; Scene is a small retained pixel scene
// for a native window. A program can use it directly through
// `native.Window.present_scene()` without making the terminal renderer learn
// about pixels, fonts, rounded corners, or ordinary GUI spacing.

import "andy/color" as color;
import "andy/style" as style;

fun packed(value) {
  if (value == nil) { return "-1"; }
  const n = value.packed();
  if (n == nil) { return "-1"; }
  return str(n);
}

fun clean(value) {
  return str(value).replace("\n", " ").replace("\r", " ");
}

class Scene {
  init(width = 800, height = 600) {
    this.width = width;
    this.height = height;
    this.commands = [];
  }

  clear(background = color.rgb(0xf4, 0xf5, 0xf7)) {
    return this.fill(0, 0, this.width, this.height, background);
  }

  fill(x, y, width, height, background = color.DEFAULT) {
    this.commands.push("pfill " + str(x) + " " + str(y) + " " +
                       str(width) + " " + str(height) + " " +
                       packed(background));
    return this;
  }

  rect(x, y, width, height, foreground = color.DEFAULT,
       background = color.DEFAULT, radius = 6, stroke = 1) {
    this.commands.push("prect " + str(x) + " " + str(y) + " " +
                       str(width) + " " + str(height) + " " + str(radius) +
                       " " + str(stroke) + " " + packed(foreground) + " " +
                       packed(background));
    return this;
  }

  line(x, y, x2, y2, foreground = color.DEFAULT, stroke = 1) {
    this.commands.push("pline " + str(x) + " " + str(y) + " " +
                       str(x2) + " " + str(y2) + " " + str(stroke) + " " +
                       packed(foreground));
    return this;
  }

  text(x, y, value, foreground = color.DEFAULT, size = 14, attrs = 0) {
    this.commands.push("ptext " + str(x) + " " + str(y) + " " + str(size) +
                       " " + packed(foreground) + " " + str(attrs) + " " +
                       clean(value));
    return this;
  }

  // Common native controls are convenience compositions, not terminal
  // glyphs. They leave room for a future event/identifier layer while
  // already giving native windows proper pixel geometry and typography.
  button(x, y, width, height, label,
         foreground = color.rgb(0xff, 0xff, 0xff),
         background = color.rgb(0x2f, 0x6f, 0xd8)) {
    this.rect(x, y, width, height, background, background, 7, 1);
    this.text(x + 14, y + floor(height / 2) - 8, label, foreground, 14,
              style.NORMAL);
    return this;
  }

  input(x, y, width, height, value = "",
        foreground = color.rgb(0x20, 0x22, 0x26),
        background = color.rgb(0xff, 0xff, 0xff),
        border = color.rgb(0xc9, 0xcd, 0xd4)) {
    this.rect(x, y, width, height, border, background, 5, 1);
    this.text(x + 10, y + floor(height / 2) - 8, value, foreground, 14);
    return this;
  }

  checkbox(x, y, checked, label,
           foreground = color.rgb(0x20, 0x22, 0x26),
           accent = color.rgb(0x2f, 0x6f, 0xd8)) {
    let background = color.rgb(0xff, 0xff, 0xff);
    if (checked) { background = accent; }
    this.rect(x, y, 18, 18, accent, background, 4, 1);
    if (checked) { this.text(x + 3, y - 1, "✓", color.WHITE, 16); }
    this.text(x + 28, y, label, foreground, 14);
    return this;
  }

  cursor(x, y, width = 2, height = 18, foreground = color.rgb(0x2f, 0x6f, 0xd8)) {
    this.commands.push("pcursor " + str(x) + " " + str(y) + " " +
                       str(width) + " " + str(height) + " " +
                       packed(foreground));
    return this;
  }

  lines() {
    const out = ["pixel " + str(this.width) + " " + str(this.height)];
    for (let command in this.commands) { out.push(command); }
    out.push("pixel_end");
    return out;
  }
}
