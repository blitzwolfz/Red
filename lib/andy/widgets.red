// Containers and controls.
//
//   import "andy/widgets" as ui;
//
//   const form = ui.Column(
//     ui.Label("Name"),
//     ui.Input().named("name"),
//     ui.Row(ui.Button("Save", save), ui.Button("Cancel", cancel)),
//   ).padded(geom.uniform(1));
//
// Every constructor here takes its children or its text first and its
// options afterwards, and returns the widget, so that a whole interface
// is one expression with the shape of the thing it describes.
//
// The containers are in the first half and the controls in the second.
// Lists, tables, trees, tabs and menus are in andy/views, because this
// file was long enough.

import "andy/geom" as geom;
import "andy/event" as event;
import "andy/style" as style;
import "andy/text" as text;
import "andy/color" as color;
import "andy/layout" as layout;
import "andy/widget" as widget;

const Widget = widget.Widget;
const Rect = geom.Rect;
const Size = geom.Size;
const Item = layout.Item;
const Align = layout.Align;
const Justify = layout.Justify;

// ---------------------------------------------------------------------
// Containers
// ---------------------------------------------------------------------

// Nothing, taking up room. A spacer with flex pushes what is after it to
// the far end, which is how a status bar gets a clock on the right.
//
//   ui.Row(ui.Label("left"), ui.Spacer(), ui.Label("right"))
class Spacer < Widget {
  init(size = 0, weight = 1) {
    super.init();
    this.size = size;
    this.flex = weight;
  }

  measure_content(available) { return Size(this.size, this.size); }
}

// A fixed gap, which is a spacer that does not grow.
fun Gap(size = 1) { return Spacer(size, 0); }

// Children laid out along one axis.
//
// Not usually built directly: Row and Column below are this with the
// axis chosen, and they read better at the call site.
class Line < Widget {
  init(vertical, children) {
    super.init();
    this.vertical = vertical;
    this.gap = 0;
    this.align = Align.Stretch;
    this.justify = Justify.Start;
    for (let child in children) { this.add(child); }
  }

  // The space between children. Set once on the container rather than
  // as a margin on each child, because the gaps between things are a
  // property of the arrangement and not of the things.
  spaced(amount) {
    this.gap = amount;
    this.invalidate();
    return this;
  }

  // How children sit across the axis: Stretch fills, Center centres,
  // Start and End go to one edge.
  aligned(value) {
    this.align = value;
    this.invalidate();
    return this;
  }

  // What happens to room left over along the axis.
  justified(value) {
    this.justify = value;
    this.invalidate();
    return this;
  }

  visible_children() {
    const out = [];
    for (let child in this.children) {
      if (child.visible) { out.push(child); }
    }
    return out;
  }

  measure_content(available) {
    const shown = this.visible_children();
    if (shown.len() == 0) { return Size(0, 0); }
    let main = this.gap * (shown.len() - 1);
    let cross = 0;
    for (let child in shown) {
      const size = child.measure(available);
      if (this.vertical) {
        main += size.height;
        cross = max(cross, size.width);
      } else {
        main += size.width;
        cross = max(cross, size.height);
      }
    }
    if (this.vertical) { return Size(cross, main); }
    return Size(main, cross);
  }

  arrange_children(area) {
    const shown = this.visible_children();
    if (shown.len() == 0) { return this; }

    // Measure against the room actually available, not against what the
    // parent offered: a child that wraps its text needs to know how wide
    // it really is before it can say how tall it is.
    const items = [];
    const crossSizes = [];
    for (let child in shown) {
      const size = child.measure(area.size());
      if (this.vertical) {
        items.push(Item(size.height, child.flex, child.min_height,
                        child.max_height));
        crossSizes.push(size.width);
      } else {
        items.push(Item(size.width, child.flex, child.min_width,
                        child.max_width));
        crossSizes.push(size.height);
      }
    }

    let total = area.width;
    if (this.vertical) { total = area.height; }
    const room = total - this.gap * (shown.len() - 1);
    const sizes = layout.distribute(max(0, room), items);
    const starts = layout.positions_for(this.justify, sizes, total, this.gap);

    let places = layout.place_horizontal(area, sizes, starts, crossSizes,
                                         this.align);
    if (this.vertical) {
      places = layout.place_vertical(area, sizes, starts, crossSizes,
                                     this.align);
    }
    for (let i in range(0, shown.len())) {
      // A child measured along the cross axis may still be bounded on
      // it, which place_* does not know about.
      let place = places[i];
      if (this.vertical and shown[i].max_width != nil) {
        place = place.resized(min(place.width, shown[i].max_width),
                              place.height);
      }
      if (!this.vertical and shown[i].max_height != nil) {
        place = place.resized(place.width,
                              min(place.height, shown[i].max_height));
      }
      shown[i].arrange(place);
    }
    return this;
  }
}

// Children side by side.
fun Row(...children) { return Line(false, children); }

// Children stacked downwards.
fun Column(...children) { return Line(true, children); }

// Children on top of each other, each given the whole area. The last one
// is on top, both for drawing and for the mouse. Dialogs, overlays and
// anything that floats is a Stack.
class Stack < Widget {
  init(...children) {
    super.init();
    for (let child in children) { this.add(child); }
  }

  measure_content(available) {
    let size = Size(0, 0);
    for (let child in this.children) {
      if (!child.visible) { continue; }
      size = size.union(child.measure(available));
    }
    return size;
  }

  arrange_children(area) {
    for (let child in this.children) {
      if (!child.visible) { continue; }
      // A child that asked for less than the whole area and is not
      // growing is centred in it, which is what a dialog wants. One
      // that grows fills it, which is what a background wants.
      if (child.flex > 0) {
        child.arrange(area);
        continue;
      }
      const wanted = child.measure(area.size());
      child.arrange(area.center(wanted));
    }
    return this;
  }
}

// One child, in the middle of whatever room there is.
class Center < Widget {
  init(child = nil) {
    super.init();
    this.horizontal = true;
    this.vertical = true;
    if (child != nil) { this.add(child); }
  }

  // Centre on one axis only, leaving the other filled.
  only(horizontal, vertical) {
    this.horizontal = horizontal;
    this.vertical = vertical;
    return this;
  }

  arrange_children(area) {
    for (let child in this.children) {
      if (!child.visible) { continue; }
      const wanted = child.measure(area.size());
      let width = area.width;
      let height = area.height;
      let x = area.x;
      let y = area.y;
      if (this.horizontal) {
        width = min(wanted.width, area.width);
        x = area.x + floor((area.width - width) / 2);
      }
      if (this.vertical) {
        height = min(wanted.height, area.height);
        y = area.y + floor((area.height - height) / 2);
      }
      child.arrange(Rect(x, y, width, height));
    }
    return this;
  }
}

// A framed area with an optional title: the box everything else sits in.
//
//   ui.Panel("Settings", ui.Column(...))
//
// The frame is drawn in the accent colour when the focus is anywhere
// inside it, which on a screen with several panes is how the user finds
// out where the keyboard went.
class Panel < Widget {
  init(title = "", child = nil) {
    super.init();
    this.title = title;
    this.title_align = "left";
    this.border_set = nil;
    this.bordered = true;
    this.fill = "surface";
    if (child != nil) { this.add(child); }
  }

  titled(value, align = "left") {
    this.title = value;
    this.title_align = align;
    this.invalidate();
    return this;
  }

  // A different border set from the theme's, for the rare case where
  // one pane really should look different from the others.
  with_border(value) {
    this.border_set = value;
    this.invalidate();
    return this;
  }

  // No frame at all: a panel that is only a background and some padding.
  borderless() {
    this.bordered = false;
    this.invalidate();
    return this;
  }

  // A border eats one cell on each side, and so does the padding, which
  // is why this is not simply `padding`: the caller may still want
  // padding of their own inside the frame.
  frame_insets() {
    if (!this.bordered) { return geom.NONE; }
    return geom.uniform(1);
  }

  measure_content(available) {
    const insets = this.frame_insets();
    const inner = Size(max(0, available.width - insets.horizontal()),
                       max(0, available.height - insets.vertical()));
    let size = Size(0, 0);
    for (let child in this.children) {
      if (!child.visible) { continue; }
      size = size.union(child.measure(inner));
    }
    let width = size.width + insets.horizontal();
    // A title needs room for itself and a space either side, or the box
    // is drawn narrower than its own caption.
    if (this.bordered and this.title != "") {
      width = max(width, text.width(this.title) + 4);
    }
    return Size(width, size.height + insets.vertical());
  }

  arrange_children(area) {
    const inside = area.shrunk(this.frame_insets());
    for (let child in this.children) {
      if (child.visible) { child.arrange(inside); }
    }
    return this;
  }

  draw_content(surface, ui) {
    if (!this.bordered) { return this; }
    const area = Rect(0, 0, this.frame.width, this.frame.height);
    let name = "border";
    let title_name = "title";
    if (!this.enabled) {
      name = "border.disabled";
      title_name = "border.disabled";
    } else if (ui.holds_focus(this)) {
      name = "border.focused";
      title_name = "title.focused";
    }
    let set = this.border_set;
    if (set == nil) { set = ui.border(); }
    surface.titled_box(area, this.title, set, ui.style(name),
                       ui.style(title_name), this.title_align);
    return this;
  }
}

// Columns and rows described the way a table is: a list of widths, each
// a number of cells, a share written "2fr", or "auto".
//
//   ui.Grid(["auto", "1fr"], 2).add(label, input, label2, input2)
//
// Children fill the grid in order, left to right and then down. A cell
// with no child is left empty rather than closing up, so a form laid out
// this way keeps its columns aligned even when one row is short.
class Grid < Widget {
  init(specs, columns = nil, ...children) {
    super.init();
    this.specs = specs;
    this.column_count = columns;
    if (this.column_count == nil) { this.column_count = specs.len(); }
    this.column_gap = 1;
    this.row_gap = 0;
    this.row_heights = [];
    for (let child in children) { this.add(child); }
  }

  spaced(columns, rows = 0) {
    this.column_gap = columns;
    this.row_gap = rows;
    this.invalidate();
    return this;
  }

  rows() {
    if (this.column_count <= 0) { return 0; }
    return ceil(this.children.len() / this.column_count);
  }

  // The widest natural content in each column, which is what "auto"
  // wants to know.
  natural_columns(available) {
    const widths = [];
    for (let i in range(0, this.column_count)) { widths.push(0); }
    for (let i in range(0, this.children.len())) {
      const child = this.children[i];
      if (!child.visible) { continue; }
      const column = i % this.column_count;
      widths[column] = max(widths[column], child.measure(available).width);
    }
    return widths;
  }

  measure_content(available) {
    const natural = this.natural_columns(available);
    let width = this.column_gap * max(0, this.column_count - 1);
    for (let one in natural) { width += one; }

    const widths = layout.columns(this.specs, available.width, natural,
                                  this.column_gap);
    let height = 0;
    const count = this.rows();
    for (let r in range(0, count)) {
      height += this.row_height(r, widths, available);
    }
    height += this.row_gap * max(0, count - 1);
    return Size(width, height);
  }

  row_height(r, widths, available) {
    let tallest = 0;
    for (let c in range(0, this.column_count)) {
      const index = r * this.column_count + c;
      if (index >= this.children.len()) { break; }
      const child = this.children[index];
      if (!child.visible) { continue; }
      let column = 0;
      if (c < widths.len()) { column = widths[c]; }
      tallest = max(tallest,
                    child.measure(Size(column, available.height)).height);
    }
    return tallest;
  }

  arrange_children(area) {
    const natural = this.natural_columns(area.size());
    const widths = layout.columns(this.specs, area.width, natural,
                                  this.column_gap);
    const count = this.rows();
    this.row_heights = [];
    for (let r in range(0, count)) {
      this.row_heights.push(this.row_height(r, widths, area.size()));
    }

    let y = area.y;
    for (let r in range(0, count)) {
      let x = area.x;
      for (let c in range(0, this.column_count)) {
        const index = r * this.column_count + c;
        let width = 0;
        if (c < widths.len()) { width = widths[c]; }
        if (index < this.children.len()) {
          const child = this.children[index];
          if (child.visible) {
            child.arrange(Rect(x, y, width, this.row_heights[r]));
          }
        }
        x += width + this.column_gap;
      }
      y += this.row_heights[r] + this.row_gap;
    }
    return this;
  }
}

// ---------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------

// Text. Wraps when it is told to, and reports a height that accounts for
// the wrapping, which is what makes a paragraph in a column push what is
// below it down rather than overlap it.
class Label < Widget {
  init(value = "", style_name = "text") {
    super.init();
    this.value = value;
    this.style_name = style_name;
    this.align = "left";
    this.wraps = false;
    this.ellipsis = true;
  }

  set_text(value) {
    if (this.value == value) { return this; }
    this.value = value;
    this.invalidate();
    return this;
  }

  styled(name) {
    this.style_name = name;
    this.invalidate();
    return this;
  }

  aligned(value) {
    this.align = value;
    this.invalidate();
    return this;
  }

  wrapping(value = true) {
    this.wraps = value;
    this.invalidate();
    return this;
  }

  lines_for(width) {
    if (this.wraps) { return text.wrap(this.value, max(1, width)); }
    return this.value.split("\n");
  }

  measure_content(available) {
    if (this.wraps and available.width > 0) {
      const lines = text.wrap(this.value, available.width);
      let widest = 0;
      for (let line in lines) { widest = max(widest, text.width(line)); }
      return Size(widest, lines.len());
    }
    const lines = this.value.split("\n");
    let widest = 0;
    for (let line in lines) { widest = max(widest, text.width(line)); }
    return Size(widest, lines.len());
  }

  draw_content(surface, ui) {
    const area = this.inner();
    let base = ui.style(this.style_name);
    if (!this.enabled) { base = ui.style("muted"); }
    const lines = this.lines_for(area.width);
    for (let i in range(0, lines.len())) {
      if (i >= area.height) { break; }
      let line = lines[i];
      if (!this.wraps and this.ellipsis) {
        line = text.ellipsize(line, area.width);
      }
      let x = area.x;
      const w = text.width(line);
      if (this.align == "center") { x += max(0, floor((area.width - w) / 2)); }
      else if (this.align == "right") { x += max(0, area.width - w); }
      surface.text(x, area.y + i, line, base, area.width);
    }
    return this;
  }
}

// A heading, which is a label the theme draws differently.
fun Title(value) { return Label(value, "title"); }

// A line across the available width, with an optional caption in it.
class Separator < Widget {
  init(caption = "", vertical = false) {
    super.init();
    this.caption = caption;
    this.vertical = vertical;
  }

  measure_content(available) {
    if (this.vertical) { return Size(1, 0); }
    return Size(text.width(this.caption), 1);
  }

  draw_content(surface, ui) {
    const area = this.inner();
    const base = ui.style("separator");
    const set = ui.border();
    if (this.vertical) {
      surface.vline(area.x, area.y, area.height, set.vertical, base);
      return this;
    }
    surface.hline(area.x, area.y, area.width, set.horizontal, base);
    if (this.caption != "") {
      const label = " " + text.ellipsize(this.caption, max(0, area.width - 4)) + " ";
      surface.text(area.x + 2, area.y, label, ui.style("muted"));
    }
    return this;
  }
}

// Something to press.
//
//   ui.Button("Save", fun () { save(); })
//
// A button is pressed by the enter or space key when it has the
// keyboard, and by the mouse. `primary` marks the one the enter key
// should reach from anywhere in a dialog, which is a decision the
// dialog acts on rather than the button.
class Button < Widget {
  init(label = "", on_press = nil) {
    super.init();
    this.label = label;
    this.on_press = on_press;
    this.focusable = true;
    this.primary = false;
    this.align = "center";
    // Which character of the label is its shortcut, or -1. Written in
    // the label as an underscore before the letter: "_Save".
    this.shortcut_at = -1;
    this.shortcut = nil;
    this.set_label(label);
    // Set for one frame after a press, so that the button visibly
    // reacts even when what it does is instantaneous.
    this.pressed_until = 0;
  }

  // The label, with an underscore marking the shortcut letter if there
  // is one. The underscore is not drawn; the letter under it is
  // underlined, and alt and that letter press the button.
  set_label(value) {
    this.label = value;
    this.shortcut_at = -1;
    this.shortcut = nil;
    const cut = value.find("_");
    if (cut >= 0 and cut + 1 < value.len()) {
      this.label = value.sub(0, cut) + value.sub(cut + 1);
      this.shortcut_at = text.width(value.sub(0, cut));
      this.shortcut = this.label.sub(cut, cut + 1).lower();
    }
    this.invalidate();
    return this;
  }

  // Marks this as the button the enter key means. A dialog looks for
  // one of these.
  as_primary(value = true) {
    this.primary = value;
    this.invalidate();
    return this;
  }

  // The width of the label plus a space either side and the brackets.
  measure_content(available) {
    return Size(text.width(this.label) + 4, 1);
  }

  press(ui = nil) {
    if (!this.enabled) { return this; }
    this.pressed_until = 2;
    this.invalidate();
    if (this.on_press != nil) { this.on_press(this); }
    return this;
  }

  style_name(ui) {
    if (!this.enabled) { return "button.disabled"; }
    if (this.pressed_until > 0) { return "button.pressed"; }
    if (ui.is_focused(this)) { return "button.focused"; }
    if (this.hovered) { return "hover"; }
    if (this.primary) { return "button.default"; }
    return "button";
  }

  draw_content(surface, ui) {
    const area = this.inner();
    if (area.is_empty()) { return this; }
    if (this.pressed_until > 0) { this.pressed_until -= 1; }
    const base = ui.style(this.style_name(ui));

    // The brackets say "this is a button" on a screen with no colour,
    // and do no harm on one with colour.
    let left = " ";
    let right = " ";
    if (ui.is_focused(this) or this.primary) {
      left = "‹";
      right = "›";
    }
    const room = max(0, area.width - 2);
    const label = text.ellipsize(this.label, room);
    let pad = max(0, room - text.width(label));
    let before = floor(pad / 2);
    if (this.align == "left") { before = 0; }
    else if (this.align == "right") { before = pad; }

    surface.fill(area, " ", base);
    surface.text(area.x, area.y, left, base);
    surface.text(area.x + area.width - 1, area.y, right, base);
    const x = area.x + 1 + before;
    surface.text(x, area.y, label, base, room);
    if (this.shortcut_at >= 0 and this.enabled and
        this.shortcut_at < text.width(label)) {
      surface.restyle(Rect(x + this.shortcut_at, area.y, 1, 1),
                      base.plus(style.UNDERLINE));
    }
    return this;
  }

  on_key_event(key, ui) {
    if (!this.enabled) { return false; }
    if (key.matches("enter") or key.matches("space")) {
      this.press(ui);
      return true;
    }
    return false;
  }

  on_mouse_event(mouse, ui) {
    if (!this.enabled) { return false; }
    if (mouse.is_press() and mouse.button == 1) {
      if (ui.app != nil) { ui.app.focus_on(this); }
      this.press(ui);
      return true;
    }
    return false;
  }
}

// A box that is either ticked or not.
class Checkbox < Widget {
  init(label = "", checked = false, on_change = nil) {
    super.init();
    this.label = label;
    this.checked = checked;
    this.on_change = on_change;
    this.focusable = true;
  }

  set_checked(value) {
    if (this.checked == value) { return this; }
    this.checked = value;
    this.changed();
    return this;
  }

  toggle() { return this.set_checked(!this.checked); }

  // The value, for a form that reads its controls by id.
  get_value() { return this.checked; }
  set_value(value) { return this.set_checked(value == true); }

  measure_content(available) {
    return Size(text.width(this.label) + 4, 1);
  }

  mark() {
    if (this.checked) { return "[x]"; }
    return "[ ]";
  }

  draw_content(surface, ui) {
    const area = this.inner();
    let base = ui.style("text");
    if (!this.enabled) { base = ui.style("button.disabled"); }
    else if (ui.is_focused(this)) { base = ui.style("selection"); }
    else if (this.hovered) { base = ui.style("hover"); }
    surface.fill(area, " ", base);
    surface.text(area.x, area.y, this.mark(), base);
    surface.text(area.x + 4, area.y,
                 text.ellipsize(this.label, max(0, area.width - 4)), base,
                 max(0, area.width - 4));
    return this;
  }

  on_key_event(key, ui) {
    if (!this.enabled) { return false; }
    if (key.matches("space") or key.matches("enter")) {
      this.toggle();
      return true;
    }
    return false;
  }

  on_mouse_event(mouse, ui) {
    if (!this.enabled) { return false; }
    if (mouse.is_press() and mouse.button == 1) {
      if (ui.app != nil) { ui.app.focus_on(this); }
      this.toggle();
      return true;
    }
    return false;
  }
}

// A checkbox drawn as a switch, for a setting that is on or off rather
// than a choice that is made or not.
class Switch < Checkbox {
  init(label = "", checked = false, on_change = nil) {
    super.init(label, checked, on_change);
  }

  measure_content(available) {
    return Size(text.width(this.label) + 6, 1);
  }

  draw_content(surface, ui) {
    const area = this.inner();
    let base = ui.style("text");
    if (!this.enabled) { base = ui.style("button.disabled"); }
    else if (ui.is_focused(this)) { base = ui.style("accent"); }
    surface.fill(area, " ", base);
    let on = ui.style("success");
    let off = ui.style("muted");
    if (!this.enabled) {
      on = base;
      off = base;
    }
    if (this.checked) {
      surface.text(area.x, area.y, "▐██▌", on);
    } else {
      surface.text(area.x, area.y, "▐  ▌", off);
    }
    surface.text(area.x + 5, area.y,
                 text.ellipsize(this.label, max(0, area.width - 5)), base,
                 max(0, area.width - 5));
    return this;
  }
}

// One of several. A radio belongs to a group, and setting one clears the
// others, which is the only thing that makes it different from a
// checkbox.
class Radio < Widget {
  init(label = "", value = nil) {
    super.init();
    this.label = label;
    this.value = value;
    if (this.value == nil) { this.value = label; }
    this.selected = false;
    this.focusable = true;
    this.group = nil;
  }

  measure_content(available) {
    return Size(text.width(this.label) + 4, 1);
  }

  choose() {
    if (!this.enabled) { return this; }
    if (this.group != nil) { this.group.select(this.value); }
    else if (!this.selected) {
      this.selected = true;
      this.changed();
    }
    return this;
  }

  mark() {
    if (this.selected) { return "(•)"; }
    return "( )";
  }

  draw_content(surface, ui) {
    const area = this.inner();
    let base = ui.style("text");
    if (!this.enabled) { base = ui.style("button.disabled"); }
    else if (ui.is_focused(this)) { base = ui.style("selection"); }
    else if (this.hovered) { base = ui.style("hover"); }
    surface.fill(area, " ", base);
    surface.text(area.x, area.y, this.mark(), base);
    surface.text(area.x + 4, area.y,
                 text.ellipsize(this.label, max(0, area.width - 4)), base,
                 max(0, area.width - 4));
    return this;
  }

  on_key_event(key, ui) {
    if (!this.enabled) { return false; }
    if (key.matches("space") or key.matches("enter")) {
      this.choose();
      return true;
    }
    return false;
  }

  on_mouse_event(mouse, ui) {
    if (!this.enabled) { return false; }
    if (mouse.is_press() and mouse.button == 1) {
      if (ui.app != nil) { ui.app.focus_on(this); }
      this.choose();
      return true;
    }
    return false;
  }
}

// A column of radios where exactly one is chosen.
//
//   ui.RadioGroup(["Small", "Medium", "Large"], "Medium")
//
// The arrow keys move between the options and choose as they go, which
// is what a radio group does everywhere else and what somebody using one
// will expect.
class RadioGroup < Widget {
  init(labels = nil, chosen = nil, on_change = nil) {
    super.init();
    this.value = chosen;
    this.on_change = on_change;
    this.vertical = true;
    if (labels != nil) {
      for (let label in labels) { this.option(label); }
    }
    this.select(chosen);
  }

  // Lays the options out side by side instead of down the page.
  horizontal() {
    this.vertical = false;
    this.invalidate();
    return this;
  }

  option(label, value = nil) {
    const button = Radio(label, value);
    button.group = this;
    this.add(button);
    if (this.value == nil) { this.select(button.value); }
    return this;
  }

  options() { return this.children; }

  select(value) {
    if (value == nil) { return this; }
    let found = false;
    for (let child in this.children) {
      const wanted = child.value == value;
      if (child.selected != wanted) { child.invalidate(); }
      child.selected = wanted;
      if (wanted) { found = true; }
    }
    if (!found) { return this; }
    if (this.value != value) {
      this.value = value;
      this.changed();
    }
    return this;
  }

  get_value() { return this.value; }
  set_value(value) { return this.select(value); }

  selected_index() {
    for (let i in range(0, this.children.len())) {
      if (this.children[i].selected) { return i; }
    }
    return -1;
  }

  step(by) {
    const count = this.children.len();
    if (count == 0) { return this; }
    let at = this.selected_index();
    if (at < 0) { at = 0; }
    else { at = geom.clamp(at + by, 0, count - 1); }
    this.select(this.children[at].value);
    return this;
  }

  measure_content(available) {
    let main = 0;
    let cross = 0;
    for (let child in this.children) {
      const size = child.measure(available);
      if (this.vertical) {
        main += size.height;
        cross = max(cross, size.width);
      } else {
        main += size.width + 1;
        cross = max(cross, size.height);
      }
    }
    if (this.vertical) { return Size(cross, main); }
    return Size(max(0, main - 1), cross);
  }

  arrange_children(area) {
    let at = 0;
    for (let child in this.children) {
      const size = child.measure(area.size());
      if (this.vertical) {
        child.arrange(Rect(area.x, area.y + at, area.width, 1));
        at += size.height;
      } else {
        child.arrange(Rect(area.x + at, area.y, size.width, 1));
        at += size.width + 1;
      }
    }
    return this;
  }
}

// A proportion, drawn as a bar. The eighth block characters give eight
// times the resolution of a bar drawn in whole cells, which on a bar
// twenty cells wide is the difference between a percentage that moves
// and one that sits still for five seconds at a time.
class ProgressBar < Widget {
  init(value = 0, total = 1) {
    super.init();
    this.value = value;
    this.total = total;
    this.show_percent = true;
    this.label = "";
    // An indeterminate bar has no number to show and animates instead,
    // for work whose length is not known.
    this.indeterminate = false;
    this.phase = 0;
  }

  set_value(value, total = nil) {
    if (total != nil) { this.total = total; }
    if (this.value == value) { return this; }
    this.value = value;
    this.invalidate();
    return this;
  }

  fraction() {
    if (this.total <= 0) { return 0; }
    return geom.clamp(this.value / this.total, 0, 1);
  }

  measure_content(available) { return Size(10, 1); }

  draw_content(surface, ui) {
    const area = this.inner();
    if (area.is_empty()) { return this; }
    const track = ui.style("progress.track");
    const filled = ui.style("progress");
    surface.fill(area, " ", track);

    let caption = this.label;
    if (this.show_percent and !this.indeterminate and caption == "") {
      caption = str(round(this.fraction() * 100)) + "%";
    }
    let width = area.width;
    if (caption != "") { width = max(0, area.width - text.width(caption) - 1); }

    if (this.indeterminate) {
      // A block sliding back and forth. The phase advances on every
      // frame it is drawn, so it moves at the speed of the event loop
      // and needs no clock of its own.
      this.phase += 1;
      const span = max(1, floor(width / 4));
      const travel = max(1, width - span);
      let at = this.phase % (travel * 2);
      if (at > travel) { at = travel * 2 - at; }
      surface.fill(Rect(area.x + at, area.y, span, 1), "█", filled);
    } else {
      const exact = this.fraction() * width;
      const whole = floor(exact);
      surface.hline(area.x, area.y, whole, "█", filled);
      const eighths = round((exact - whole) * 8);
      if (eighths > 0 and whole < width) {
        surface.set(area.x + whole, area.y,
                    style.BLOCKS_HORIZONTAL[eighths], filled, 1);
      }
    }
    if (caption != "") {
      surface.text(area.x + area.width - text.width(caption), area.y,
                   caption, ui.style("muted"));
    }
    return this;
  }
}

// A number chosen by dragging or with the arrow keys.
class Slider < Widget {
  init(value = 0, low = 0, high = 100, step = 1, on_change = nil) {
    super.init();
    this.low = low;
    this.high = high;
    this.step = step;
    this.value = geom.clamp(value, low, high);
    this.on_change = on_change;
    this.focusable = true;
    this.show_value = true;
  }

  set_value(value) {
    const next = geom.clamp(value, this.low, this.high);
    if (next == this.value) { return this; }
    this.value = next;
    this.changed();
    return this;
  }

  get_value() { return this.value; }

  fraction() {
    if (this.high <= this.low) { return 0; }
    return (this.value - this.low) / (this.high - this.low);
  }

  measure_content(available) {
    let width = 12;
    if (this.show_value) { width += text.width(str(this.high)) + 1; }
    return Size(width, 1);
  }

  track_width(area) {
    if (!this.show_value) { return area.width; }
    return max(1, area.width - text.width(str(this.high)) - 1);
  }

  draw_content(surface, ui) {
    const area = this.inner();
    if (area.is_empty()) { return this; }
    const width = this.track_width(area);
    let track = ui.style("progress.track");
    let handle = ui.style("accent");
    if (!this.enabled) {
      track = ui.style("button.disabled");
      handle = track;
    } else if (ui.is_focused(this)) {
      handle = ui.style("selection");
    }
    surface.hline(area.x, area.y, width, "─", track);
    const at = round(this.fraction() * max(0, width - 1));
    surface.set(area.x + at, area.y, "●", handle, 1);
    if (this.show_value) {
      const caption = text.pad_left(str(this.value),
                                    text.width(str(this.high)));
      surface.text(area.x + width + 1, area.y, caption, ui.style("muted"));
    }
    return this;
  }

  on_key_event(key, ui) {
    if (!this.enabled) { return false; }
    if (key.matches("left") or key.matches("down")) {
      this.set_value(this.value - this.step);
      return true;
    }
    if (key.matches("right") or key.matches("up")) {
      this.set_value(this.value + this.step);
      return true;
    }
    if (key.matches("home")) {
      this.set_value(this.low);
      return true;
    }
    if (key.matches("end")) {
      this.set_value(this.high);
      return true;
    }
    if (key.matches("pageup")) {
      this.set_value(this.value + this.step * 10);
      return true;
    }
    if (key.matches("pagedown")) {
      this.set_value(this.value - this.step * 10);
      return true;
    }
    return false;
  }

  on_mouse_event(mouse, ui) {
    if (!this.enabled) { return false; }
    if (mouse.is_wheel()) {
      this.set_value(this.value - mouse.wheel * this.step);
      return true;
    }
    if (!mouse.is_press() and !mouse.is_drag()) { return false; }
    if (ui.app != nil and mouse.is_press()) { ui.app.focus_on(this); }
    const area = this.inner();
    const width = max(1, this.track_width(area) - 1);
    const at = geom.clamp(mouse.x - area.x, 0, width);
    const span = this.high - this.low;
    let value = this.low + span * at / width;
    if (this.step > 0) {
      value = this.low + round((value - this.low) / this.step) * this.step;
    }
    this.set_value(value);
    return true;
  }
}

// A line of text the user can edit.
//
//   ui.Input("", "your name").named("name")
//
// The text is edited as clusters rather than as bytes, so a name with an
// accent in it deletes one character at a time and a name in kanji moves
// two columns at a time, which is what somebody typing either of them
// expects.
//
// There is a selection, an undo history and the usual bindings: the
// arrow keys with and without control, home and end, ctrl+a and ctrl+e
// for the same, ctrl+w to delete a word, ctrl+u and ctrl+k to delete to
// an end, ctrl+z and ctrl+y for undo and redo.
class Input < Widget {
  init(value = "", placeholder = "", on_change = nil) {
    super.init();
    this.cells = text.clusters(value);
    this.placeholder = placeholder;
    this.on_change = on_change;
    this.focusable = true;
    // Where the caret is, counted in clusters, from 0 to len().
    this.caret = this.cells.len();
    // The other end of the selection, or -1 when there is none.
    this.anchor = -1;
    // The first cluster shown, for a value wider than the field.
    this.offset = 0;
    // Drawn instead of the real characters, for a password.
    this.mask = nil;
    this.max_length = nil;
    // Called with the proposed text; return false to refuse it. A field
    // that only accepts digits is three lines rather than a subclass.
    this.accepts = nil;
    this.on_submit = nil;
    this.history = [];
    this.future = [];
  }

  // ---- the value ----

  get_value() {
    const parts = [];
    for (let [cluster, w] in this.cells) { parts.push(cluster); }
    return parts.join("");
  }

  set_value(value) {
    const next = str(value);
    if (next == this.get_value()) { return this; }
    this.remember();
    this.cells = text.clusters(next);
    this.caret = this.cells.len();
    this.anchor = -1;
    this.offset = 0;
    this.changed();
    return this;
  }

  // Restricts what may be typed. The test is given the whole proposed
  // value, not the character, so that "a number between 0 and 100" can
  // be written as what it is.
  accepting(test) {
    this.accepts = test;
    return this;
  }

  // Shows `character` instead of what was typed.
  masked(character = "•") {
    this.mask = character;
    this.invalidate();
    return this;
  }

  limited(count) {
    this.max_length = count;
    return this;
  }

  // Called when the enter key is pressed in the field.
  on_submitted(handler) {
    this.on_submit = handler;
    return this;
  }

  // ---- selection ----

  has_selection() { return this.anchor >= 0 and this.anchor != this.caret; }

  selection_range() {
    if (!this.has_selection()) { return [this.caret, this.caret]; }
    return [min(this.caret, this.anchor), max(this.caret, this.anchor)];
  }

  selected_text() {
    const [from, to] = this.selection_range();
    const parts = [];
    for (let i in range(from, to)) { parts.push(this.cells[i][0]); }
    return parts.join("");
  }

  select_all() {
    this.anchor = 0;
    this.caret = this.cells.len();
    this.invalidate();
    return this;
  }

  clear_selection() {
    this.anchor = -1;
    return this;
  }

  // ---- editing ----

  // Keeps the current value so that ctrl+z can come back to it. The
  // history is capped: an editor that remembers everything forever is an
  // editor that eventually is nothing but history.
  remember() {
    this.history.push([this.get_value(), this.caret]);
    if (this.history.len() > 200) { this.history.remove(0); }
    this.future.clear();
    return this;
  }

  undo() {
    if (this.history.len() == 0) { return this; }
    const [value, caret] = this.history.pop();
    this.future.push([this.get_value(), this.caret]);
    this.cells = text.clusters(value);
    this.caret = geom.clamp(caret, 0, this.cells.len());
    this.anchor = -1;
    this.changed();
    return this;
  }

  redo() {
    if (this.future.len() == 0) { return this; }
    const [value, caret] = this.future.pop();
    this.history.push([this.get_value(), this.caret]);
    this.cells = text.clusters(value);
    this.caret = geom.clamp(caret, 0, this.cells.len());
    this.anchor = -1;
    this.changed();
    return this;
  }

  // Replaces the selection, or inserts at the caret. Everything that
  // changes the text goes through here, so the length limit and the
  // acceptance test are checked in one place and cannot be forgotten.
  insert(value) {
    if (!this.enabled or value == "") { return this; }
    const [from, to] = this.selection_range();
    const added = text.clusters(text.sanitize(value));
    const kept = [];
    for (let i in range(0, from)) { kept.push(this.cells[i]); }
    for (let cell in added) { kept.push(cell); }
    for (let i in range(to, this.cells.len())) { kept.push(this.cells[i]); }
    if (this.max_length != nil and kept.len() > this.max_length) {
      return this;
    }
    const parts = [];
    for (let [cluster, w] in kept) { parts.push(cluster); }
    const proposed = parts.join("");
    if (this.accepts != nil and !this.accepts(proposed)) { return this; }

    this.remember();
    this.cells = kept;
    this.caret = from + added.len();
    this.anchor = -1;
    this.changed();
    return this;
  }

  // Removes the selection, or `count` clusters from the caret. A
  // negative count deletes backwards.
  erase(count = 1) {
    if (!this.enabled) { return this; }
    let from = 0;
    let to = 0;
    if (this.has_selection()) {
      const range = this.selection_range();
      from = range[0];
      to = range[1];
    } else if (count < 0) {
      from = max(0, this.caret + count);
      to = this.caret;
    } else {
      from = this.caret;
      to = min(this.cells.len(), this.caret + count);
    }
    if (from == to) { return this; }

    this.remember();
    const kept = [];
    for (let i in range(0, from)) { kept.push(this.cells[i]); }
    for (let i in range(to, this.cells.len())) { kept.push(this.cells[i]); }
    this.cells = kept;
    this.caret = from;
    this.anchor = -1;
    this.changed();
    return this;
  }

  // ---- moving about ----

  move_to(where, extend = false) {
    const next = geom.clamp(where, 0, this.cells.len());
    if (extend) {
      if (this.anchor < 0) { this.anchor = this.caret; }
    } else {
      this.anchor = -1;
    }
    this.caret = next;
    this.invalidate();
    return this;
  }

  // The start of the word before the caret, or the end of the one after
  // it. Words are runs of anything that is not a space, which is the
  // definition every terminal program uses and is close enough to right.
  word_boundary(direction) {
    let at = this.caret;
    if (direction < 0) {
      while (at > 0 and this.cells[at - 1][0] == " ") { at -= 1; }
      while (at > 0 and this.cells[at - 1][0] != " ") { at -= 1; }
      return at;
    }
    const count = this.cells.len();
    while (at < count and this.cells[at][0] == " ") { at += 1; }
    while (at < count and this.cells[at][0] != " ") { at += 1; }
    return at;
  }

  // ---- drawing ----

  measure_content(available) { return Size(12, 1); }

  // The column each cluster starts at, so that the caret and the
  // selection land in the right place when the text has wide characters
  // in it.
  columns_before(index) {
    let total = 0;
    for (let i in range(this.offset, min(index, this.cells.len()))) {
      total += this.cells[i][1];
    }
    return total;
  }

  // Scrolls so that the caret is inside the field. Called while drawing,
  // because that is the first moment the width is known.
  reveal(width) {
    if (this.caret < this.offset) { this.offset = this.caret; }
    if (width <= 0) { return this; }
    for (;;) {
      if (this.columns_before(this.caret) < width) { break; }
      if (this.offset >= this.cells.len()) { break; }
      this.offset += 1;
    }
    return this;
  }

  style_name(ui) {
    if (!this.enabled) { return "input.disabled"; }
    if (ui.is_focused(this)) { return "input.focused"; }
    return "input";
  }

  draw_content(surface, ui) {
    const area = this.inner();
    if (area.is_empty()) { return this; }
    const base = ui.style(this.style_name(ui));
    surface.fill(area, " ", base);
    this.reveal(area.width);

    const empty = this.cells.len() == 0;
    if (empty and this.placeholder != "" and !ui.is_focused(this)) {
      surface.text(area.x, area.y,
                   text.ellipsize(this.placeholder, area.width),
                   ui.style("input.placeholder"), area.width);
      return this;
    }

    let x = area.x;
    const [from, to] = this.selection_range();
    const chosen = ui.style("input.selection");
    for (let i in range(this.offset, this.cells.len())) {
      const [cluster, advance] = this.cells[i];
      if (x + advance > area.x + area.width) { break; }
      let shown = cluster;
      let cell = base;
      if (this.mask != nil) { shown = this.mask; }
      if (this.has_selection() and i >= from and i < to) { cell = chosen; }
      surface.set(x, area.y, shown, cell, advance);
      x += advance;
    }

    if (ui.is_focused(this) and this.enabled) {
      const at = area.x + this.columns_before(this.caret);
      if (at < area.x + area.width) { surface.place_cursor(at, area.y); }
    }
    return this;
  }

  // ---- keys ----

  on_key_event(key, ui) {
    if (!this.enabled) { return false; }

    if (key.is_text()) {
      this.insert(key.text);
      return true;
    }

    const shift = key.shift;
    if (key.matches("left")) { return this.stepped(-1, false); }
    if (key.matches("shift+left")) { return this.stepped(-1, true); }
    if (key.matches("right")) { return this.stepped(1, false); }
    if (key.matches("shift+right")) { return this.stepped(1, true); }
    if (key.matches("ctrl+left") or key.matches("alt+b")) {
      this.move_to(this.word_boundary(-1), false);
      return true;
    }
    if (key.matches("ctrl+right") or key.matches("alt+f")) {
      this.move_to(this.word_boundary(1), false);
      return true;
    }
    if (key.matches("home") or key.matches("ctrl+a")) {
      this.move_to(0, shift);
      return true;
    }
    if (key.matches("end") or key.matches("ctrl+e")) {
      this.move_to(this.cells.len(), shift);
      return true;
    }
    if (key.matches("backspace")) {
      this.erase(-1);
      return true;
    }
    if (key.matches("delete") or key.matches("ctrl+d")) {
      this.erase(1);
      return true;
    }
    if (key.matches("ctrl+w") or key.matches("alt+backspace")) {
      const to = this.word_boundary(-1);
      this.anchor = this.caret;
      this.caret = to;
      this.erase();
      return true;
    }
    if (key.matches("ctrl+u")) {
      this.anchor = 0;
      this.erase();
      return true;
    }
    if (key.matches("ctrl+k")) {
      this.anchor = this.cells.len();
      this.erase();
      return true;
    }
    if (key.matches("ctrl+z")) {
      this.undo();
      return true;
    }
    if (key.matches("ctrl+y")) {
      this.redo();
      return true;
    }
    if (key.matches("enter")) {
      if (this.on_submit != nil) {
        this.on_submit(this);
        return true;
      }
      return false;
    }
    return false;
  }

  stepped(by, extend) {
    // An arrow key with a selection and no shift puts the caret at the
    // end of the selection rather than moving one from there, which is
    // what every other text field does.
    if (!extend and this.has_selection()) {
      const [from, to] = this.selection_range();
      if (by < 0) { this.move_to(from, false); } else { this.move_to(to, false); }
      return true;
    }
    this.move_to(this.caret + by, extend);
    return true;
  }

  on_mouse_event(mouse, ui) {
    if (!this.enabled) { return false; }
    if (!mouse.is_press() and !mouse.is_drag()) { return false; }
    if (mouse.is_press() and ui.app != nil) { ui.app.focus_on(this); }
    const area = this.inner();
    let column = mouse.x - area.x;
    let at = this.offset;
    while (at < this.cells.len() and column >= this.cells[at][1]) {
      column -= this.cells[at][1];
      at += 1;
    }
    this.move_to(at, mouse.is_drag());
    return true;
  }

  on_focus(ui) {
    // Arriving by keyboard selects everything, so that typing replaces
    // the old value. That is what a form expects; a field being clicked
    // into puts the caret where it was clicked instead, and the mouse
    // handler above runs after this and does exactly that.
    this.select_all();
    return super.on_focus(ui);
  }
}

// A field that holds a number.
fun NumberInput(value = 0, low = nil, high = nil) {
  const field = Input(str(value));
  field.accepting(fun (proposed) {
    if (proposed == "" or proposed == "-") { return true; }
    const parsed = num(proposed);
    if (parsed == nil) { return false; }
    if (low != nil and parsed < low) { return false; }
    if (high != nil and parsed > high) { return false; }
    return true;
  });
  return field;
}

// Several lines of text the user can edit.
//
// The same editing model as Input, on an array of lines. Long lines
// scroll sideways rather than wrapping, because a wrapped line makes
// "up" and "down" mean two different things and an editor that gets that
// wrong is worse than one that scrolls.
class TextArea < Widget {
  init(value = "", on_change = nil) {
    super.init();
    this.lines = value.split("\n");
    this.on_change = on_change;
    this.focusable = true;
    this.row = 0;
    this.column = 0;
    this.top = 0;
    this.left = 0;
    this.read_only = false;
    // The column the caret would like to be in, kept across up and down
    // so that moving through a short line and out the other side comes
    // back to where it started.
    this.wanted_column = 0;
    this.history = [];
    this.future = [];
  }

  get_value() { return this.lines.join("\n"); }

  set_value(value) {
    const next = str(value);
    if (next == this.get_value()) { return this; }
    this.remember();
    this.lines = next.split("\n");
    this.row = 0;
    this.column = 0;
    this.top = 0;
    this.left = 0;
    this.changed();
    return this;
  }

  read_only_at(value = true) {
    this.read_only = value;
    return this;
  }

  remember() {
    this.history.push([this.get_value(), this.row, this.column]);
    if (this.history.len() > 200) { this.history.remove(0); }
    this.future.clear();
    return this;
  }

  undo() {
    if (this.history.len() == 0) { return this; }
    const [value, row, column] = this.history.pop();
    this.future.push([this.get_value(), this.row, this.column]);
    this.lines = value.split("\n");
    this.row = geom.clamp(row, 0, this.lines.len() - 1);
    this.column = geom.clamp(column, 0, this.current().len());
    this.changed();
    return this;
  }

  current() { return text.clusters(this.lines[this.row]); }

  // Puts a row of clusters back into the line it came from.
  put_row(row, cells) {
    const parts = [];
    for (let [cluster, w] in cells) { parts.push(cluster); }
    this.lines[row] = parts.join("");
    return this;
  }

  measure_content(available) { return Size(20, 5); }

  insert(value) {
    if (this.read_only) { return this; }
    this.remember();
    for (let piece in text.sanitize(value).split("\n")) {
      const cells = this.current();
      const kept = [];
      for (let i in range(0, this.column)) { kept.push(cells[i]); }
      for (let cell in text.clusters(piece)) { kept.push(cell); }
      const tail = [];
      for (let i in range(this.column, cells.len())) { tail.push(cells[i]); }
      for (let cell in tail) { kept.push(cell); }
      this.put_row(this.row, kept);
      this.column += text.clusters(piece).len();
    }
    this.changed();
    return this;
  }

  // Splits the line at the caret, which is what the enter key does.
  break_line() {
    if (this.read_only) { return this; }
    this.remember();
    const cells = this.current();
    const head = [];
    for (let i in range(0, this.column)) { head.push(cells[i]); }
    const tail = [];
    for (let i in range(this.column, cells.len())) { tail.push(cells[i]); }
    this.put_row(this.row, head);
    const parts = [];
    for (let [cluster, w] in tail) { parts.push(cluster); }
    this.lines.insert(this.row + 1, parts.join(""));
    this.row += 1;
    this.column = 0;
    this.changed();
    return this;
  }

  erase_back() {
    if (this.read_only) { return this; }
    if (this.column > 0) {
      this.remember();
      const cells = this.current();
      cells.remove(this.column - 1);
      this.put_row(this.row, cells);
      this.column -= 1;
      this.changed();
      return this;
    }
    if (this.row == 0) { return this; }
    // At the start of a line, join it to the one above.
    this.remember();
    const above = this.lines[this.row - 1];
    this.column = text.clusters(above).len();
    this.lines[this.row - 1] = above + this.lines[this.row];
    this.lines.remove(this.row);
    this.row -= 1;
    this.changed();
    return this;
  }

  erase_forward() {
    if (this.read_only) { return this; }
    const cells = this.current();
    if (this.column < cells.len()) {
      this.remember();
      cells.remove(this.column);
      this.put_row(this.row, cells);
      this.changed();
      return this;
    }
    if (this.row + 1 >= this.lines.len()) { return this; }
    this.remember();
    this.lines[this.row] += this.lines[this.row + 1];
    this.lines.remove(this.row + 1);
    this.changed();
    return this;
  }

  move(dr, dc, keep_column = false) {
    if (dr != 0) {
      this.row = geom.clamp(this.row + dr, 0, this.lines.len() - 1);
      this.column = min(this.wanted_column, this.current().len());
    } else {
      this.column = this.column + dc;
      if (this.column < 0) {
        if (this.row > 0) {
          this.row -= 1;
          this.column = this.current().len();
        } else {
          this.column = 0;
        }
      } else if (this.column > this.current().len()) {
        if (this.row + 1 < this.lines.len()) {
          this.row += 1;
          this.column = 0;
        } else {
          this.column = this.current().len();
        }
      }
      if (!keep_column) { this.wanted_column = this.column; }
    }
    this.invalidate();
    return this;
  }

  // Scrolls so the caret is on screen.
  reveal(area) {
    if (this.row < this.top) { this.top = this.row; }
    if (this.row >= this.top + area.height) {
      this.top = this.row - area.height + 1;
    }
    const cells = this.current();
    let column = 0;
    for (let i in range(0, min(this.column, cells.len()))) {
      column += cells[i][1];
    }
    if (column < this.left) { this.left = column; }
    if (column >= this.left + area.width) {
      this.left = column - area.width + 1;
    }
    return column;
  }

  draw_content(surface, ui) {
    const area = this.inner();
    if (area.is_empty()) { return this; }
    let base = ui.style("input");
    if (!this.enabled) { base = ui.style("input.disabled"); }
    surface.fill(area, " ", base);
    const column = this.reveal(area);

    for (let i in range(0, area.height)) {
      const r = this.top + i;
      if (r >= this.lines.len()) { break; }
      let x = area.x;
      let at = 0;
      for (let [cluster, advance] in text.clusters(this.lines[r])) {
        if (at >= this.left) {
          if (x + advance > area.x + area.width) { break; }
          surface.set(x, area.y + i, cluster, base, advance);
          x += advance;
        }
        at += advance;
      }
    }
    if (ui.is_focused(this) and this.enabled) {
      surface.place_cursor(area.x + column - this.left,
                           area.y + this.row - this.top);
    }
    return this;
  }

  on_key_event(key, ui) {
    if (!this.enabled) { return false; }
    if (key.is_text()) {
      this.insert(key.text);
      return true;
    }
    if (key.matches("up")) { this.move(-1, 0); return true; }
    if (key.matches("down")) { this.move(1, 0); return true; }
    if (key.matches("left")) { this.move(0, -1); return true; }
    if (key.matches("right")) { this.move(0, 1); return true; }
    if (key.matches("home") or key.matches("ctrl+a")) {
      this.column = 0;
      this.wanted_column = 0;
      this.invalidate();
      return true;
    }
    if (key.matches("end") or key.matches("ctrl+e")) {
      this.column = this.current().len();
      this.wanted_column = this.column;
      this.invalidate();
      return true;
    }
    if (key.matches("pageup")) {
      this.move(-max(1, this.frame.height - 1), 0);
      return true;
    }
    if (key.matches("pagedown")) {
      this.move(max(1, this.frame.height - 1), 0);
      return true;
    }
    if (key.matches("enter")) { this.break_line(); return true; }
    if (key.matches("backspace")) { this.erase_back(); return true; }
    if (key.matches("delete")) { this.erase_forward(); return true; }
    if (key.matches("ctrl+z")) { this.undo(); return true; }
    return false;
  }

  on_mouse_event(mouse, ui) {
    if (!this.enabled) { return false; }
    if (mouse.is_wheel()) {
      this.top = geom.clamp(this.top + mouse.wheel * 3, 0,
                            max(0, this.lines.len() - 1));
      this.invalidate();
      return true;
    }
    if (!mouse.is_press()) { return false; }
    if (ui.app != nil) { ui.app.focus_on(this); }
    const area = this.inner();
    this.row = geom.clamp(this.top + mouse.y - area.y, 0,
                          this.lines.len() - 1);
    let column = mouse.x - area.x + this.left;
    const cells = this.current();
    let at = 0;
    while (at < cells.len() and column >= cells[at][1]) {
      column -= cells[at][1];
      at += 1;
    }
    this.column = at;
    this.wanted_column = at;
    this.invalidate();
    return true;
  }
}

// One child, seen through a smaller window, with scrollbars.
//
//   ui.Scroll(ui.Column(...))
//
// The child is measured as if it had all the room it asked for and
// arranged at that size, and then moved under the window. That is what
// makes a scrolling view show a real layout rather than a special one.
class Scroll < Widget {
  init(child = nil) {
    super.init();
    this.offset_x = 0;
    this.offset_y = 0;
    this.horizontal = false;
    this.vertical = true;
    this.show_bars = true;
    this.content = Size(0, 0);
    this.focusable = true;
    if (child != nil) { this.add(child); }
  }

  // Scrolls sideways as well. Off by default: a view that scrolls both
  // ways is usually a layout that has gone wrong.
  both_ways(value = true) {
    this.horizontal = value;
    return this;
  }

  child() {
    if (this.children.len() == 0) { return nil; }
    return this.children[0];
  }

  measure_content(available) {
    const one = this.child();
    if (one == nil) { return Size(0, 0); }
    return one.measure(available);
  }

  // The room the child is given, which is unbounded on whichever axis
  // scrolls.
  content_area(area) {
    const one = this.child();
    if (one == nil) { return Rect(0, 0, 0, 0); }
    let width = area.width;
    let height = area.height;
    if (this.show_bars and this.vertical) { width = max(0, width - 1); }
    if (this.horizontal) { width = 100000; }
    if (this.vertical) { height = 100000; }
    const wanted = one.measure(Size(width, height));
    let w = wanted.width;
    let h = wanted.height;
    if (!this.horizontal) {
      w = area.width;
      if (this.show_bars and this.vertical and wanted.height > area.height) {
        w = max(0, area.width - 1);
      }
    }
    if (!this.vertical) { h = area.height; }
    return Rect(0, 0, max(w, 0), max(h, 0));
  }

  arrange_children(area) {
    const one = this.child();
    if (one == nil) { return this; }
    const content = this.content_area(area);
    this.content = content.size();
    this.clamp_offsets(area);
    one.arrange(Rect(area.x - this.offset_x, area.y - this.offset_y,
                     content.width, content.height));
    return this;
  }

  clamp_offsets(area) {
    this.offset_y = geom.clamp(this.offset_y, 0,
                               max(0, this.content.height - area.height));
    this.offset_x = geom.clamp(this.offset_x, 0,
                               max(0, this.content.width - area.width));
    return this;
  }

  scroll_by(dx, dy) {
    this.offset_x += dx;
    this.offset_y += dy;
    this.invalidate();
    return this;
  }

  scroll_to(x, y) {
    this.offset_x = x;
    this.offset_y = y;
    this.invalidate();
    return this;
  }

  draw(surface, ui) {
    if (!this.visible or this.frame.is_empty()) { return this; }
    if (this.fill != nil) {
      surface.fill(Rect(0, 0, this.frame.width, this.frame.height), " ",
                   ui.style(this.fill));
    }
    const area = this.inner();
    const one = this.child();
    if (one != nil) {
      // Clip rather than frame: the child has already been arranged at
      // its scrolled position, so its coordinates are right and only the
      // edges need cutting.
      surface.clipped(area, fun () {
        surface.frame(one.frame, fun () { one.draw(surface, ui); });
      });
    }
    if (this.show_bars) { this.draw_bars(surface, ui, area); }
    return this;
  }

  // A scrollbar whose thumb is proportional to how much is showing and
  // never shorter than one cell, so that a very long document still has
  // something to grab.
  draw_bars(surface, ui, area) {
    const track = ui.style("scrollbar");
    const thumb = ui.style("scrollbar.thumb");
    if (this.vertical and this.content.height > area.height) {
      const x = area.right() - 1;
      surface.vline(x, area.y, area.height, "│", track);
      const size = max(1, round(area.height * area.height / this.content.height));
      const span = max(1, this.content.height - area.height);
      const at = round((area.height - size) * this.offset_y / span);
      surface.vline(x, area.y + at, size, "█", thumb);
    }
    if (this.horizontal and this.content.width > area.width) {
      const y = area.bottom() - 1;
      surface.hline(area.x, y, area.width, "─", track);
      const size = max(1, round(area.width * area.width / this.content.width));
      const span = max(1, this.content.width - area.width);
      const at = round((area.width - size) * this.offset_x / span);
      surface.hline(area.x + at, y, size, "█", thumb);
    }
    return this;
  }

  on_key_event(key, ui) {
    const area = this.inner();
    if (key.matches("up")) { return this.moved(0, -1); }
    if (key.matches("down")) { return this.moved(0, 1); }
    if (key.matches("pageup")) { return this.moved(0, -max(1, area.height - 1)); }
    if (key.matches("pagedown")) { return this.moved(0, max(1, area.height - 1)); }
    if (key.matches("home")) {
      this.offset_y = 0;
      this.invalidate();
      return true;
    }
    if (key.matches("end")) {
      this.offset_y = max(0, this.content.height - area.height);
      this.invalidate();
      return true;
    }
    if (this.horizontal and key.matches("left")) { return this.moved(-1, 0); }
    if (this.horizontal and key.matches("right")) { return this.moved(1, 0); }
    return false;
  }

  moved(dx, dy) {
    const before = [this.offset_x, this.offset_y];
    this.scroll_by(dx, dy);
    this.clamp_offsets(this.inner());
    return before[0] != this.offset_x or before[1] != this.offset_y;
  }

  on_mouse_event(mouse, ui) {
    if (mouse.is_wheel()) { return this.moved(0, mouse.wheel * 3); }
    return false;
  }
}

// A choice made from a list that appears when it is opened.
//
//   ui.Select(["Red", "Green", "Blue"], "Green")
//
// The list is drawn over whatever is below it, which is why a Select
// needs an application: it asks for an overlay rather than drawing one
// itself, so that it appears above its own container's frame.
class Select < Widget {
  init(options = nil, chosen = nil, on_change = nil) {
    super.init();
    this.options = [];
    if (options != nil) { this.options = options; }
    this.value = chosen;
    if (this.value == nil and this.options.len() > 0) {
      this.value = this.options[0];
    }
    this.on_change = on_change;
    this.focusable = true;
    this.open = false;
    this.highlight = 0;
    this.placeholder = "";
    this.max_visible = 8;
  }

  set_options(options, chosen = nil) {
    this.options = options;
    if (chosen != nil) { this.value = chosen; }
    if (!options.contains(this.value)) {
      this.value = nil;
      if (options.len() > 0) { this.value = options[0]; }
    }
    this.invalidate();
    return this;
  }

  get_value() { return this.value; }

  set_value(value) {
    if (this.value == value) { return this; }
    this.value = value;
    this.changed();
    return this;
  }

  label_of(option) { return str(option); }

  measure_content(available) {
    let widest = text.width(this.placeholder);
    for (let option in this.options) {
      widest = max(widest, text.width(this.label_of(option)));
    }
    return Size(widest + 3, 1);
  }

  index_of_value() {
    for (let i in range(0, this.options.len())) {
      if (this.options[i] == this.value) { return i; }
    }
    return -1;
  }

  toggle() {
    this.open = !this.open;
    if (this.open) {
      this.highlight = max(0, this.index_of_value());
    }
    this.invalidate();
    return this;
  }

  // How tall the open list would be, which the application needs in
  // order to decide whether to put it above or below.
  popup_height() {
    return min(this.max_visible, max(1, this.options.len())) + 2;
  }

  draw_content(surface, ui) {
    const area = this.inner();
    if (area.is_empty()) { return this; }
    let base = ui.style("input");
    if (!this.enabled) { base = ui.style("input.disabled"); }
    else if (ui.is_focused(this)) { base = ui.style("selection"); }
    surface.fill(area, " ", base);

    let caption = this.placeholder;
    let caption_style = ui.style("input.placeholder");
    if (this.value != nil) {
      caption = this.label_of(this.value);
      caption_style = base;
    }
    surface.text(area.x, area.y, text.ellipsize(caption, max(0, area.width - 2)),
                 caption_style, max(0, area.width - 2));
    let arrow = "▾";
    if (this.open) { arrow = "▴"; }
    surface.text(area.right() - 1, area.y, arrow, base);
    return this;
  }

  // The open list, drawn by the application into an overlay so that it
  // sits above everything else.
  draw_popup(surface, ui, area) {
    const base = ui.style("menu");
    surface.fill(area, " ", base);
    surface.box(area, ui.border(), ui.style("dialog.border"));
    const inner = area.shrunk(geom.uniform(1));
    const count = min(inner.height, this.options.len());
    let first = 0;
    if (this.highlight >= count) { first = this.highlight - count + 1; }
    for (let i in range(0, count)) {
      const index = first + i;
      if (index >= this.options.len()) { break; }
      let row = base;
      if (index == this.highlight) { row = ui.style("menu.selected"); }
      const label = text.ellipsize(this.label_of(this.options[index]),
                                   inner.width);
      surface.fill(Rect(inner.x, inner.y + i, inner.width, 1), " ", row);
      surface.text(inner.x, inner.y + i, label, row, inner.width);
    }
    return this;
  }

  choose_highlighted() {
    if (this.highlight >= 0 and this.highlight < this.options.len()) {
      this.set_value(this.options[this.highlight]);
    }
    this.open = false;
    this.invalidate();
    return this;
  }

  on_key_event(key, ui) {
    if (!this.enabled) { return false; }
    if (!this.open) {
      if (key.matches("enter") or key.matches("space") or key.matches("down")) {
        this.toggle();
        return true;
      }
      // Closed, the arrow keys step through the options directly, which
      // is quicker than opening the list for a choice of three.
      if (key.matches("up")) {
        const at = this.index_of_value();
        if (at > 0) { this.set_value(this.options[at - 1]); }
        return true;
      }
      return false;
    }
    if (key.matches("escape")) {
      this.open = false;
      this.invalidate();
      return true;
    }
    if (key.matches("enter") or key.matches("space")) {
      this.choose_highlighted();
      return true;
    }
    if (key.matches("up")) {
      this.highlight = geom.clamp(this.highlight - 1, 0,
                                  max(0, this.options.len() - 1));
      this.invalidate();
      return true;
    }
    if (key.matches("down")) {
      this.highlight = geom.clamp(this.highlight + 1, 0,
                                  max(0, this.options.len() - 1));
      this.invalidate();
      return true;
    }
    // A letter jumps to the first option starting with it, which is how
    // a long list is used without a search box.
    if (key.is_text()) {
      const wanted = key.text.lower();
      for (let i in range(0, this.options.len())) {
        if (this.label_of(this.options[i]).lower().starts_with(wanted)) {
          this.highlight = i;
          this.invalidate();
          return true;
        }
      }
    }
    return false;
  }

  on_mouse_event(mouse, ui) {
    if (!this.enabled) { return false; }
    if (mouse.is_press() and mouse.button == 1) {
      if (ui.app != nil) { ui.app.focus_on(this); }
      this.toggle();
      return true;
    }
    return false;
  }

  on_blur(ui) {
    this.open = false;
    return super.on_blur(ui);
  }
}
