// Deciding how big things are and where they go.
//
//   import "andy/layout" as layout;
//
// Layout in andy is two passes, the way every layout worth using is.
//
// **Measure.** A widget is asked how big it would like to be, given how
// much room there is. It answers with a size, having asked its children
// the same question first. Nothing moves and nothing is drawn.
//
// **Arrange.** A widget is given a rectangle — usually not the one it
// asked for — and puts itself and its children in it.
//
// Asking first and telling afterwards is what lets a column of things
// give each one the height it needs, and lets a label inside a scrolling
// view report how tall it would be if it had the room.
//
// The functions here are the arithmetic that containers share. They take
// numbers and return numbers, and know nothing about widgets, which is
// what makes them straightforward to test and to reason about.

import "andy/geom" as geom;

// How a thing sits in space left over around it.
enum Align { Start, Center, End, Stretch }

// What a row or a column does with room it has left after every child
// has what it needs.
enum Justify { Start, Center, End, Between, Around, Evenly }

// One thing to be laid out along a line.
//
// `natural` is what it asked for. `flex` is its share of whatever is
// left over: zero means it gets exactly what it asked for, and a larger
// number means a larger share. `minimum` and `maximum` bound the result,
// and a maximum of nil means no bound.
class Item {
  init(natural, flex = 0, minimum = 0, maximum = nil) {
    this.natural = max(0, natural);
    this.flex = max(0, flex);
    this.minimum = max(0, minimum);
    this.maximum = maximum;
  }

  bounded(value) {
    let result = max(this.minimum, value);
    if (this.maximum != nil) { result = min(this.maximum, result); }
    return max(0, result);
  }

  str() { return "Item(${this.natural}, flex ${this.flex})"; }
}

// Divides `total` between `items` and gives back a size for each.
//
// Everything gets what it asked for if there is room. Room left over is
// shared out in proportion to flex; room that is short is taken away in
// proportion to how much each one asked for, because taking it evenly
// would starve the small ones first.
//
// Both passes repeat while any item is pinned by a bound, since pinning
// one changes what the others are owed. That loop terminates: each pass
// either pins something, of which there are finitely many, or finishes.
//
// The sizes are whole numbers and add up to exactly `total` when there
// is flex to absorb the difference, so a row of panes fills its width
// with no gap and no overlap, which a naive rounding does not manage.
fun distribute(total, items) {
  const count = items.len();
  if (count == 0) { return []; }

  const sizes = [];
  let wanted = 0;
  for (let item in items) {
    const size = item.bounded(item.natural);
    sizes.push(size);
    wanted += size;
  }

  let left = total - wanted;
  if (left == 0) { return sizes; }

  if (left > 0) {
    return grow(sizes, items, left);
  }
  return shrink(sizes, items, -left);
}

// Shares `extra` among the items with flex, in proportion to it, and
// leaves the rest alone. An item that hits its maximum drops out and its
// share is offered to the others.
fun grow(sizes, items, extra) {
  const open = [];
  for (let i in range(0, items.len())) {
    if (items[i].flex > 0) { open.push(i); }
  }
  if (open.len() == 0) { return sizes; }

  let remaining = extra;
  while (remaining > 0 and open.len() > 0) {
    let weight = 0;
    for (let i in open) { weight += items[i].flex; }
    if (weight <= 0) { break; }

    const pinned = [];
    let given = 0;
    // Shares are cut from a running total rather than rounded one at a
    // time, so the rounding is spread across the row instead of piling
    // up on whichever item happens to be last, and the sizes still add
    // up to exactly what there was to give.
    let ideal = 0;
    let issued = 0;
    for (let n in range(0, open.len())) {
      const i = open[n];
      ideal += remaining * items[i].flex / weight;
      const share = round(ideal) - issued;
      issued += share;
      const want = sizes[i] + share;
      const allowed = items[i].bounded(want);
      if (allowed < want) { pinned.push(i); }
      given += allowed - sizes[i];
      sizes[i] = allowed;
    }
    remaining -= given;
    if (pinned.len() == 0) { break; }
    // Try again with the pinned ones out of the running.
    const still = [];
    for (let i in open) {
      if (!pinned.contains(i)) { still.push(i); }
    }
    if (still.len() == open.len()) { break; }
    open.clear();
    for (let i in still) { open.push(i); }
  }
  return sizes;
}

// Takes `deficit` away, in two passes.
//
// **Flexible items give way first, in proportion to what they have.** An
// item with flex said it would take a share of whatever there was, and a
// share of too little is less. Three panes in half the room they asked
// for become three half sized panes.
//
// **Whatever is still owed is taken from the end.** Items with no flex
// asked for a size rather than a share, so there is no proportion to
// honour, and something has to go. What goes is the last of them: a
// column too short for its contents should lose what is at the bottom,
// which is the part nobody has read yet, and a row too narrow should
// show the first thing whole rather than three things in pieces.
//
// Nothing is taken below an item's minimum in either pass.
fun shrink(sizes, items, deficit) {
  let remaining = shrink_flexible(sizes, items, deficit);
  if (remaining <= 0) { return sizes; }

  for (let n in range(0, items.len())) {
    if (remaining <= 0) { break; }
    const i = items.len() - 1 - n;
    const room = sizes[i] - items[i].minimum;
    if (room <= 0) { continue; }
    const take = min(room, remaining);
    sizes[i] -= take;
    remaining -= take;
  }
  return sizes;
}

// The proportional pass. Gives back what it could not take, either
// because nothing had flex or because everything reached its minimum.
fun shrink_flexible(sizes, items, deficit) {
  let remaining = deficit;
  const open = [];
  for (let i in range(0, items.len())) {
    if (items[i].flex > 0 and sizes[i] > items[i].minimum) { open.push(i); }
  }

  while (remaining > 0 and open.len() > 0) {
    let available = 0;
    for (let i in open) { available += sizes[i] - items[i].minimum; }
    if (available <= 0) { break; }
    const take = min(remaining, available);

    const pinned = [];
    let taken = 0;
    // Shares are cut from a running total rather than rounded one at a
    // time, so the rounding is spread across the row instead of piling
    // up on whichever item happens to be last.
    let ideal = 0;
    let issued = 0;
    for (let n in range(0, open.len())) {
      const i = open[n];
      const room = sizes[i] - items[i].minimum;
      ideal += take * room / available;
      let share = round(ideal) - issued;
      if (share > room) { share = room; }
      if (share < 0) { share = 0; }
      issued += share;
      sizes[i] -= share;
      taken += share;
      if (sizes[i] <= items[i].minimum) { pinned.push(i); }
    }
    remaining -= taken;
    if (taken == 0) { break; }
    if (pinned.len() == 0) { continue; }
    const still = [];
    for (let i in open) {
      if (!pinned.contains(i)) { still.push(i); }
    }
    open.clear();
    for (let i in still) { open.push(i); }
  }
  return remaining;
}

// Where a run of `size` sits inside `total`, given an alignment. Stretch
// has no answer here — it changes the size rather than the position —
// so it is treated as Start, and the caller resizes separately.
fun offset_for(align, size, total) {
  switch (align) {
    case Align.Center: return max(0, floor((total - size) / 2));
    case Align.End: return max(0, total - size);
  }
  return 0;
}

// Where each of several runs sits along a line, given how the leftover
// room should be spread. Returns the starting position of each, and the
// gap between them is built into those positions rather than reported
// separately, because Around and Evenly put different gaps in different
// places.
fun positions_for(justify, sizes, total, gap = 0) {
  const count = sizes.len();
  if (count == 0) { return []; }
  let used = gap * (count - 1);
  for (let size in sizes) { used += size; }
  const spare = total - used;

  let start = 0;
  let between = gap;
  if (spare > 0) {
    switch (justify) {
      case Justify.Center: start = floor(spare / 2);
      case Justify.End: start = spare;
      case Justify.Between: {
        if (count > 1) { between = gap + floor(spare / (count - 1)); }
        else { start = 0; }
      }
      case Justify.Around: {
        const each = floor(spare / count);
        start = floor(each / 2);
        between = gap + each;
      }
      case Justify.Evenly: {
        const each = floor(spare / (count + 1));
        start = each;
        between = gap + each;
      }
    }
  }

  const out = [];
  let at = start;
  for (let i in range(0, count)) {
    out.push(at);
    at += sizes[i] + between;
  }
  return out;
}

// The columns of a grid, from a set of specifications.
//
// A specification is a number for a fixed width, a number followed by
// "fr" for a share of what is left, or "auto" for whatever the content
// needs. `natural` gives the content width of each column, which is
// what "auto" is asking about.
//
//   layout.columns(["10", "1fr", "auto", "2fr"], 60, naturals)
//
// This is the same distribution as a row, with the specifications turned
// into items first, and it exists so that a table's columns can be
// described in one readable line.
fun columns(specs, total, natural = nil, gap = 0) {
  const items = [];
  for (let i in range(0, specs.len())) {
    const spec = str(specs[i]).trim().lower();
    let content = 0;
    if (natural != nil and i < natural.len()) { content = natural[i]; }
    if (spec == "auto") {
      items.push(Item(content, 0, 0, nil));
      continue;
    }
    if (spec.ends_with("fr")) {
      const weight = num(spec.sub(0, spec.len() - 2));
      let flex = 1;
      if (weight != nil) { flex = weight; }
      items.push(Item(0, flex, 0, nil));
      continue;
    }
    const fixed = num(spec);
    if (fixed == nil) {
      items.push(Item(content, 0, 0, nil));
      continue;
    }
    items.push(Item(floor(fixed), 0, floor(fixed), floor(fixed)));
  }
  const room = total - gap * max(0, specs.len() - 1);
  return distribute(max(0, room), items);
}

// Rectangles for a row of sizes along the top edge of `area`, using the
// cross axis alignment for their height. The helper containers use this;
// it is here because a grid's rows want it too.
fun place_horizontal(area, sizes, starts, cross_sizes, align) {
  const out = [];
  for (let i in range(0, sizes.len())) {
    let height = cross_sizes[i];
    if (align == Align.Stretch) { height = area.height; }
    const y = area.y + offset_for(align, height, area.height);
    out.push(geom.Rect(area.x + starts[i], y, sizes[i], height));
  }
  return out;
}

fun place_vertical(area, sizes, starts, cross_sizes, align) {
  const out = [];
  for (let i in range(0, sizes.len())) {
    let width = cross_sizes[i];
    if (align == Align.Stretch) { width = area.width; }
    const x = area.x + offset_for(align, width, area.width);
    out.push(geom.Rect(x, area.y + starts[i], width, sizes[i]));
  }
  return out;
}
