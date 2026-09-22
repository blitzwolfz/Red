// What everything looks like, in one place.
//
//   import "andy/theme" as theme;
//
//   const dark = theme.DARK;
//   canvas.text(0, 0, "hello", dark.get("text"));
//
// A theme is a map from a name to a style. Widgets ask for names —
// "button.focused", "input.placeholder" — and never name a colour, so
// one program can be recoloured without any of its widgets knowing, and
// a widget written by somebody else fits in with the rest.
//
// A name falls back along its dots: "button.focused.label" tries that,
// then "button.focused", then "button", then "text". So a theme can say
// as little as it likes and a widget can ask as precisely as it likes,
// and the two do not have to agree in advance on where to stop.
//
// The built in themes are at the bottom: DARK, LIGHT, MONO for a screen
// with no colour, and CONTRAST for one where the usual greys are not
// distinct enough.

import "andy/color" as color;
import "andy/style" as style;

const Style = style.Style;

class Theme {
  init(name, styles = nil, border = nil) {
    this.name = name;
    this.styles = {};
    if (styles != nil) {
      for (let [key, value] in styles.entries()) { this.styles[key] = value; }
    }
    // The border set this theme draws frames with. Part of the theme
    // rather than of each widget, because a window whose panes are drawn
    // with three different line weights looks like a mistake.
    this.border_set = border;
    if (this.border_set == nil) { this.border_set = style.SINGLE; }
    this.cache = {};
  }

  // The style for a name, falling back along the dots and finally to
  // "text", which every theme defines.
  get(name) {
    const known = this.cache.get(name, nil);
    if (known != nil) { return known; }
    let key = name;
    for (;;) {
      const found = this.styles.get(key, nil);
      if (found != nil) {
        this.cache[name] = found;
        return found;
      }
      const cut = key.find(".");
      if (cut < 0) { break; }
      // Drop the last segment, not the first: "button.focused" falls
      // back to "button", which is the more general thing.
      let parts = key.split(".");
      parts.pop();
      key = parts.join(".");
      if (parts.len() == 0) { break; }
    }
    const fallback = this.styles.get("text", style.PLAIN);
    this.cache[name] = fallback;
    return fallback;
  }

  // Does the theme say anything about this name, as opposed to falling
  // back? A widget with an optional decoration asks before drawing it.
  has(name) { return this.styles.has(name); }

  // A copy with some names changed. Themes are values: changing one in
  // place would change it for every window sharing it.
  //
  //   const mine = theme.DARK.with({"accent": style.Style(color.ORANGE)});
  with(overrides, border = nil) {
    const copy = Theme(this.name, this.styles, border);
    if (border == nil) { copy.border_set = this.border_set; }
    for (let [key, value] in overrides.entries()) { copy.styles[key] = value; }
    return copy;
  }

  // The background the window is painted with, which is also what a
  // widget mixes towards when it fades something out.
  background() { return this.get("background").bg; }

  str() { return "<theme ${this.name}>"; }
}

// Builds a whole theme from a handful of decisions.
//
// Writing thirty styles by hand is how themes end up inconsistent, so a
// theme is normally described by its background, its text, one accent
// colour and a few states, and everything else is derived. A theme that
// wants to disagree with one of the derived choices overrides it with
// `with()`.
fun build(name, options) {
  const bg = options.get("background", color.rgb(0x16, 0x18, 0x1d));
  const fg = options.get("text", color.rgb(0xd8, 0xdc, 0xe4));
  const accent = options.get("accent", color.rgb(0x4a, 0x90, 0xe2));
  const surface = options.get("surface", bg.mix(fg, 0.08));
  const raised = options.get("raised", bg.mix(fg, 0.16));
  const muted = options.get("muted", fg.mix(bg, 0.45));
  const faint = options.get("faint", fg.mix(bg, 0.7));
  const danger = options.get("danger", color.rgb(0xe0, 0x5c, 0x5c));
  const warning = options.get("warning", color.rgb(0xe0, 0xa3, 0x30));
  const success = options.get("success", color.rgb(0x4c, 0xb7, 0x6a));
  const info = options.get("info", accent);
  const on_accent = options.get("on_accent", accent.contrasting());
  const border = options.get("border", style.SINGLE);

  const styles = {
    // The two every fallback ends at.
    "background": Style(fg, bg),
    "text": Style(fg, bg),

    "muted": Style(muted, bg),
    "faint": Style(faint, bg),
    "accent": Style(accent, bg),
    "surface": Style(fg, surface),
    "raised": Style(fg, raised),

    // Frames. A focused frame is drawn in the accent colour, which is
    // the cheapest way to show which pane the keyboard is talking to.
    "border": Style(muted, bg),
    "border.focused": Style(accent, bg),
    "border.disabled": Style(faint, bg),
    "title": Style(fg, bg, style.BOLD),
    "title.focused": Style(accent, bg, style.BOLD),

    // Selection. The inactive form is for a list that holds a selection
    // but does not have the keyboard: still visible, no longer shouting.
    "selection": Style(on_accent, accent),
    "selection.inactive": Style(fg, raised),
    "hover": Style(fg, surface),

    // Buttons.
    "button": Style(fg, raised),
    "button.focused": Style(on_accent, accent, style.BOLD),
    "button.pressed": Style(on_accent, accent.mix(color.BLACK, 0.25)),
    "button.disabled": Style(faint, surface),
    "button.default": Style(fg, raised, style.BOLD),

    // Text entry.
    "input": Style(fg, surface),
    "input.focused": Style(fg, surface),
    "input.disabled": Style(faint, surface),
    "input.placeholder": Style(faint, surface, style.ITALIC),
    "input.selection": Style(on_accent, accent),

    // Anything that shows a proportion.
    "progress": Style(accent, bg),
    "progress.track": Style(faint, bg),
    "scrollbar": Style(faint, bg),
    "scrollbar.thumb": Style(muted, bg),

    // Bars along an edge.
    "header": Style(fg, raised, style.BOLD),
    "status": Style(muted, surface),
    "status.key": Style(on_accent, accent, style.BOLD),
    "menu": Style(fg, raised),
    "menu.selected": Style(on_accent, accent),
    "menu.disabled": Style(faint, raised),
    "menu.shortcut": Style(muted, raised),

    "tab": Style(muted, bg),
    "tab.selected": Style(fg, bg, style.BOLD),
    "separator": Style(faint, bg),
    "shortcut": Style(accent, bg, style.BOLD),

    // Messages.
    "error": Style(danger, bg, style.BOLD),
    "warning": Style(warning, bg),
    "success": Style(success, bg),
    "info": Style(info, bg),

    // Dialogs sit on their own surface so that they read as being in
    // front of the window rather than cut out of it.
    "dialog": Style(fg, raised),
    "dialog.border": Style(accent, raised),
    "dialog.title": Style(fg, raised, style.BOLD),
    "dialog.button": Style(fg, raised.mix(fg, 0.12)),
    "dialog.button.focused": Style(on_accent, accent, style.BOLD),
    "overlay": Style(faint, bg),
  };

  for (let [key, value] in options.get("styles", {}).entries()) {
    styles[key] = value;
  }
  return Theme(name, styles, border);
}

// The default. Dark, blue, and quiet enough to look at for an afternoon.
const DARK = build("dark", {
  "background": color.rgb(0x16, 0x18, 0x1d),
  "text": color.rgb(0xd8, 0xdc, 0xe4),
  "accent": color.rgb(0x4a, 0x90, 0xe2),
  "border": style.ROUNDED,
});

const LIGHT = build("light", {
  "background": color.rgb(0xfa, 0xfa, 0xf8),
  "text": color.rgb(0x20, 0x24, 0x2c),
  "accent": color.rgb(0x1e, 0x6f, 0xd9),
  "danger": color.rgb(0xc0, 0x36, 0x36),
  "warning": color.rgb(0xa8, 0x6c, 0x10),
  "success": color.rgb(0x1f, 0x7a, 0x3d),
  "border": style.ROUNDED,
});

// For a terminal with no colour at all, and for anything being piped
// into a file or a test. Everything that would have been a colour
// becomes an attribute: bold for emphasis, reverse for selection.
//
// This is not a lesser theme. An interface that is legible here is
// legible in every terminal, which is a good thing to check before
// shipping one.
const MONO = Theme("mono", {
  "background": style.PLAIN,
  "text": style.PLAIN,
  "muted": Style(nil, nil, style.DIM),
  "faint": Style(nil, nil, style.DIM),
  "accent": Style(nil, nil, style.BOLD),
  "surface": style.PLAIN,
  "raised": style.PLAIN,

  "border": style.PLAIN,
  "border.focused": Style(nil, nil, style.BOLD),
  "border.disabled": Style(nil, nil, style.DIM),
  "title": Style(nil, nil, style.BOLD),
  "title.focused": Style(nil, nil, style.BOLD),

  "selection": Style(nil, nil, style.REVERSE),
  "selection.inactive": Style(nil, nil, style.UNDERLINE),
  "hover": Style(nil, nil, style.UNDERLINE),

  "button": style.PLAIN,
  "button.focused": Style(nil, nil, style.REVERSE),
  "button.pressed": Style(nil, nil, style.REVERSE | style.BOLD),
  "button.disabled": Style(nil, nil, style.DIM),
  "button.default": Style(nil, nil, style.BOLD),

  "input": Style(nil, nil, style.UNDERLINE),
  "input.focused": Style(nil, nil, style.UNDERLINE | style.BOLD),
  "input.disabled": Style(nil, nil, style.DIM),
  "input.placeholder": Style(nil, nil, style.DIM),
  "input.selection": Style(nil, nil, style.REVERSE),

  "progress": Style(nil, nil, style.BOLD),
  "progress.track": Style(nil, nil, style.DIM),
  "scrollbar": Style(nil, nil, style.DIM),
  "scrollbar.thumb": style.PLAIN,

  "header": Style(nil, nil, style.REVERSE),
  "status": Style(nil, nil, style.DIM),
  "status.key": Style(nil, nil, style.BOLD),
  "menu": style.PLAIN,
  "menu.selected": Style(nil, nil, style.REVERSE),
  "menu.disabled": Style(nil, nil, style.DIM),
  "menu.shortcut": Style(nil, nil, style.DIM),

  "tab": Style(nil, nil, style.DIM),
  "tab.selected": Style(nil, nil, style.BOLD),
  "separator": Style(nil, nil, style.DIM),
  "shortcut": Style(nil, nil, style.BOLD | style.UNDERLINE),

  "error": Style(nil, nil, style.BOLD),
  "warning": Style(nil, nil, style.BOLD),
  "success": Style(nil, nil, style.BOLD),
  "info": style.PLAIN,

  "dialog": style.PLAIN,
  "dialog.border": Style(nil, nil, style.BOLD),
  "dialog.title": Style(nil, nil, style.BOLD),
  "dialog.button": style.PLAIN,
  "dialog.button.focused": Style(nil, nil, style.REVERSE),
  "overlay": Style(nil, nil, style.DIM),
}, style.SINGLE);

// Black, white and one colour, with nothing in between. For anybody who
// finds the usual low contrast greys hard to read, and for a screen
// being looked at across a room.
const CONTRAST = build("contrast", {
  "background": color.BLACK,
  "text": color.WHITE,
  "accent": color.rgb(0xff, 0xd0, 0x00),
  "surface": color.rgb(0x20, 0x20, 0x20),
  "raised": color.rgb(0x38, 0x38, 0x38),
  "muted": color.WHITE,
  "faint": color.rgb(0xb0, 0xb0, 0xb0),
  "on_accent": color.BLACK,
  "border": style.THICK,
});

const THEMES = {
  "dark": DARK,
  "light": LIGHT,
  "mono": MONO,
  "contrast": CONTRAST,
};

// A theme by name, or nil. `default_theme()` is what picks one when the
// program has not.
fun named(name) { return THEMES.get(name.lower(), nil); }

// The theme to use when nobody said: whatever $ANDY_THEME names, and the
// dark one otherwise. A user who prefers one can set it once for every
// program built with andy, which is the point.
fun default_theme() {
  const wanted = env("ANDY_THEME");
  if (wanted != nil) {
    const found = named(wanted);
    if (found != nil) { return found; }
  }
  return DARK;
}

// The theme that suits a screen of this depth. A colour theme on a
// terminal with no colour draws every style the same, which is worse
// than having asked for MONO in the first place.
fun for_depth(depth, preferred = nil) {
  if (preferred == nil) { preferred = default_theme(); }
  if (depth == color.Depth.None) { return MONO; }
  return preferred;
}
