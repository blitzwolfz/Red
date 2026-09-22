# andy

A user interface library. The same program draws in a terminal and in a
window of its own.

```red
import "andy" as andy;

const name = andy.Input("", "your name");

const form = andy.Column(
  andy.Label("What should I call you?"),
  name,
  andy.Row(andy.Spacer(), andy.Button("Done", fun (b) { andy.stop(); }))
).spaced(1).padded(andy.uniform(1));

andy.run(andy.Center(andy.Panel("Hello", form).sized(44, 9)),
  {"title": "Hello"});

print("hello, ${name.get_value()}");
```

That is [`examples/andy_hello.red`](../examples/andy_hello.red).

`red build` includes the shipped Red libraries in the standalone executable,
so a program importing `andy` carries its Red modules with it. A native
window backend still depends on its native extension being available at run
time. Pass `--no-stdlib` to omit unused shipped Red library modules; modules
the program imports remain bundled.

```
$ red examples/andy_hello.red                     in this terminal
$ ANDY_BACKEND=native red examples/andy_hello.red in a window
$ ANDY_THEME=light red examples/andy_hello.red    in daylight
```

[`examples/andy_demo.red`](../examples/andy_demo.red) is a program that
uses most of the library: four pages, a menu, dialogs, every widget.

```
$ red examples/andy_demo.red
```

## What it is

andy draws a **grid of cells**. A cell is one column wide and one line
tall, holds one character and two colours, and that is the whole of the
drawing model. A terminal has exactly this; the window backend draws the
same grid in a fixed pitch font. Everything above that line — the
widgets, the layout, the focus, the themes — cannot tell which it got.

A program builds a **tree of widgets** once and keeps it. Changing what
is shown means setting a field on a widget, not rebuilding a
description of the screen. That suits a program with state — a form, an
editor, a dashboard — because the state lives where it is used.

The loop **only draws when something changed**. A form sitting on the
screen with nobody touching it costs one poll and one sleep per pass.

## The modules

`import "andy"` is the front door: it re-exports everything, so
`andy.geom.Rect` works from the one import, and lifts the widgets and
`App` to the top so they are written without a prefix.

| Module | What is in it |
|---|---|
| [`andy/geom`](../lib/andy/geom.red) | Points, sizes, rectangles, insets. |
| [`andy/color`](../lib/andy/color.red) | Colour, and reducing it for the screen at hand. |
| [`andy/style`](../lib/andy/style.red) | A cell's two colours and its attributes; border sets. |
| [`andy/text`](../lib/andy/text.red) | How wide a string is, and how to cut one to fit. |
| [`andy/event`](../lib/andy/event.red) | Key presses, the mouse, resizes, pastes. |
| [`andy/theme`](../lib/andy/theme.red) | Named styles, and four themes. |
| [`andy/canvas`](../lib/andy/canvas.red) | The cell surface used by terminal-compatible widgets. |
| [`andy/layout`](../lib/andy/layout.red) | Measuring, and sharing out room. |
| [`andy/widget`](../lib/andy/widget.red) | What a widget is. |
| [`andy/widgets`](../lib/andy/widgets.red) | Containers and controls. |
| [`andy/views`](../lib/andy/views.red) | Lists, tables, trees, tabs, menus, bars. |
| [`andy/backend`](../lib/andy/backend.red) | What a screen has to do; a screen that is not one. |
| [`andy/term`](../lib/andy/term.red) | The terminal backend. |
| [`andy/native`](../lib/andy/native.red) | The native window transport. |
| [`andy/native_gui`](../lib/andy/native_gui.red) | Pixel scenes and native GUI primitives. |
| [`andy/app`](../lib/andy/app.red) | The loop, the focus, the dialogs. |

## Running one

```red
andy.run(root, options)
```

| Option | Meaning |
|---|---|
| `title` | The window's title, and the terminal's while it runs. |
| `backend` | `"auto"`, `"terminal"`, `"native"` or `"headless"`. |
| `theme` | A theme, or a name. Defaults to `$ANDY_THEME`, then dark. |
| `cols` `rows` | The compatibility grid size of a new window. Ignored by the terminal. |
| `quit` | A key that stops the program. Defaults to `ctrl+q`. |
| `screen` | A backend to use as it stands, instead of choosing one. |

`"auto"` prefers the terminal, because a program started from a shell
should draw where it was started, and falls back to a window when there
is no terminal — which is what happens when it is launched from a
desktop.

| Variable | What it does |
|---|---|
| `ANDY_BACKEND` | Overrides `backend`. `terminal`, `native`, `headless`. |
| `ANDY_THEME` | `dark`, `light`, `mono`, `contrast`. |
| `ANDY_COLOR` | `none`, `16`, `256`, `truecolor`. Overrides the guess. |
| `NO_COLOR` | Set to anything: the monochrome theme, always. |
| `ANDY_GUI` | Where `andy-gui` is, if it is not beside the interpreter. |
| `ANDY_FONT_SIZE` | The window's font size, on macOS. |
| `ANDY_X11_FONT` | The window's font, as an X11 font name. |
| `ANDY_TRACE` | A file to append every frame to, for debugging. |

`andy.backends()` says which are usable here, and `andy.stop()` ends the
application `run()` started, which is what a Quit button calls.

## Building a tree

Every constructor takes its children or its text first and its options
afterwards, and returns the widget, so an interface is one expression
with the shape of the thing it describes.

```red
const form = andy.Column(
  andy.Row(andy.Label("Name"), andy.Input().growing(1)),
  andy.Row(andy.Label("Note"), andy.Input().growing(1))
).spaced(1).padded(andy.uniform(1));
```

Red has no trailing commas in an argument list, so the last child of a
container has none.

These are on every widget:

| Call | What it does |
|---|---|
| `named(id)` | A name, for `app.find(id)` later. |
| `growing(n)` | Its share of leftover room along the parent's axis. |
| `padded(insets)` | Space inside it, around its contents. |
| `sized(w, h)` | Exactly this size. Either may be `nil`. |
| `at_least(w, h)` `at_most(w, h)` | A bound rather than a size. |
| `filled(style)` | A background, by theme name. |
| `shown(bool)` | Out of the layout entirely when false. |
| `set_enabled(bool)` | Greyed, and out of reach of the keyboard and mouse. |
| `on_changed(f)` | Called when the widget's value changes. |

### Containers

| Widget | What it does |
|---|---|
| `Row(...)` `Column(...)` | Children along an axis. `spaced(n)`, `aligned()`, `justified()`. |
| `Stack(...)` | Children on top of each other. The last is in front. |
| `Center(child)` | One child in the middle. |
| `Panel(title, child)` | A frame with an optional title. |
| `Grid(specs, columns, ...)` | Columns described as `10`, `1fr`, `auto`. |
| `Scroll(child)` | A window onto something larger, with scrollbars. |
| `Spacer()` `Gap(n)` | Room, with and without stretch. |

A panel's frame is drawn in the accent colour when the focus is anywhere
inside it, which on a screen with four panes is how somebody finds out
where the keyboard went.

### Controls

| Widget | Value | Notes |
|---|---|---|
| `Label(text)` | — | `wrapping()`, `aligned()`, `styled(name)`. |
| `Title(text)` | — | A label the theme draws as a heading. |
| `Separator(caption)` | — | A rule, optionally captioned. |
| `Button(label, f)` | — | `_S` in the label underlines S and makes it a shortcut. |
| `Checkbox(label, on)` | bool | |
| `Switch(label, on)` | bool | A checkbox for a setting rather than a choice. |
| `Radio(label, value)` | — | Belongs to a group. |
| `RadioGroup(labels, chosen)` | the value | `horizontal()`. |
| `Slider(value, low, high, step)` | number | Arrows, page keys, wheel, drag. |
| `ProgressBar(value, total)` | — | Fills in eighths of a cell. `indeterminate`. |
| `Input(value, placeholder)` | string | `masked()`, `limited(n)`, `accepting(f)`. |
| `NumberInput(value, low, high)` | string | An `Input` that refuses anything else. |
| `TextArea(value)` | string | Several lines. |
| `Select(options, chosen)` | the option | Opens over whatever is below it. |

### Views

| Widget | Value | Notes |
|---|---|---|
| `List(items, f)` | the item | `labelled(f)`, `detailed(f)`. |
| `CheckList(items)` | the ticked items | Space ticks. |
| `Table(headers, specs)` | the row | `aligning()`, `with_sorting()`, `celled(f)`. |
| `Tree(roots)` | the node's value | `Node(label, value)`, `loaded_by(f)`. |
| `Tabs()` | the title | `.page(title, child)`. |
| `MenuBar()` | — | `.menu(title, items)` of `MenuItem` and `Divider`. |
| `StatusBar(message)` | — | `.hint(key, what)`. |

Everything with a selection handles the keyboard the same way: the arrow
keys move, page up and down move by a screen, home and end go to the
ends, enter acts on what is selected, and typing a letter jumps to the
next entry starting with it.

A table sorts a column as numbers when both cells are numbers and as
text otherwise, without being told which a column holds.

## The application

```red
const window = andy.App(root, {"title": "Notes"});
window.bind("ctrl+s", fun () { save(); });
window.run();
```

| Call | What it does |
|---|---|
| `bind(key, f)` | A key that works wherever the focus is. |
| `find(id)` | The widget with that name, in the window or any dialog. |
| `focus_on(w)` `focus_next()` `focus_previous()` | Move the keyboard. |
| `after(s, f)` `every(s, f)` `cancel(id)` | Timers. |
| `toast(message, kind)` | A note at the bottom, for a few seconds. |
| `message(title, body, f)` | One button. |
| `confirm(title, body, f)` | Two. `f` is called with `true` or `false`. |
| `prompt(title, body, f, initial)` | A text field. `nil` when cancelled. |
| `choose(title, options, f)` | A list to pick from. |
| `open_context_menu(items, x, y)` | A menu at a point. |
| `copy(text)` | The system clipboard, when the screen has one. |
| `set_theme(t)` | Recolours everything at once. |
| `quit(code)` `request_close()` | Stop; `on_close` may refuse. |
| `step()` | One pass of the loop, for a test or a loop of your own. |

A key goes to the focused widget first, then outwards through its
parents, then to the bindings. That is what lets a dialog handle escape
without its buttons knowing what a dialog is, and lets a text field keep
its own `ctrl+a` while the program binds `ctrl+a` for something else
elsewhere.

`on_frame` is called once a frame, before drawing. `on_close` is called
when something asks the program to stop, and returning `false` refuses,
which is how "you have unsaved changes" is written.

## Themes

A theme maps a name to a style. Widgets ask for names — `"button.focused"`,
`"input.placeholder"` — and never name a colour, so a program can be
recoloured by somebody who has never read it.

A name falls back along its dots: `"button.focused.label"` tries that,
then `"button.focused"`, then `"button"`, then `"text"`. A theme can say
as little as it likes and a widget can ask as precisely as it likes.

| Theme | For |
|---|---|
| `DARK` | The default. |
| `LIGHT` | Daylight. |
| `MONO` | A terminal with no colour. Emphasis becomes bold, selection becomes reverse. |
| `CONTRAST` | Black, white and one colour, with nothing in between. |

```red
const mine = andy.theme.DARK.with({
  "accent": andy.Style(andy.rgb(0xff, 0x8c, 0x00)),
});
```

`theme.build(name, options)` makes a whole theme from a background, a
text colour, an accent and a few states, and derives the other thirty.
Writing thirty styles by hand is how themes end up inconsistent.

`MONO` is not a lesser theme. An interface that is legible there is
legible in every terminal, which is a good thing to check before
shipping one.

## Writing a widget

Four methods. A widget that overrides none of them is a blank rectangle.

A class cannot be derived from one reached through a module — `class
Spinner < andy.Widget` is a syntax error — so the base is bound to a
name first. That is a property of the language rather than of andy, and
it is the one thing about writing a widget that is not obvious.

```red
import "andy" as andy;

const Widget = andy.Widget;

class Spinner < Widget {
  init() {
    super.init();
    this.at = 0;
  }

  // How big this would like to be, given what there is.
  measure_content(available) { return andy.Size(1, 1); }

  // Drawn at 0, 0: the canvas is already framed to this widget, and
  // nothing drawn outside it will land.
  draw_content(surface, ui) {
    const frames = ["|", "/", "-", "\\"];
    surface.text(0, 0, frames[this.at % 4], ui.style("accent"));
  }

  // True when the event was dealt with and should go no further.
  on_key_event(key, ui) { return false; }
}
```

`arrange_children(area)` is the fourth, for a container deciding where
its children go. `measure` and `arrange` apply the padding and the
bounds around them, which is why the overrides are the `_content` and
`_children` ones.

A widget calls `invalidate()` when what it shows has changed, and
`changed()` when its value has, which invalidates and calls
`on_change`.

## Testing an interface

The headless backend draws into memory and takes its events from a list.
It is a real backend: the same widgets run and the same drawing happens.
Only the last step, where cells become escape sequences, is replaced by
keeping them.

```red
const screen = andy.backend.Headless(40, 10);
const window = andy.App(root);
window.screen = screen;
screen.start();
window.layout();
window.focus_first();

screen.type("ada");
window.step();
print(window.find("name").get_value());   // ada

screen.key("tab");
window.step();
print(screen.frame());                    // the screen, as text
```

`andy.render(root, width, height)` is the short form when all you want
is a picture: it measures, arranges, draws and gives back a string.
Useful for a README, and for looking at a layout without starting
anything.

[`tests/andy.red`](../tests/andy.red) is the whole library tested this
way.

## The backends

### The terminal

Switches to the terminal's alternate screen, so the scrollback a program
was launched from is exactly as it was left. Turns on mouse reporting,
bracketed paste and focus reporting, and turns all of it off again in
the same order.

Only what changed is drawn: the frame is compared with the one before
and the runs that differ are sent, with one cursor move and one colour
change per run.

It needs `andy_ext.so`, built beside the interpreter, because there is
no way to put a terminal into raw mode from Red alone. `term.available()`
says whether it loaded and whether there is a terminal to drive.

How much colour is guessed from `$COLORTERM`, `$TERM` and `$NO_COLOR`,
and `$ANDY_COLOR` overrides the guess.

### The window

A program of its own, `andy-gui`, built beside the interpreter. Cocoa on
macOS, X11 elsewhere. andy listens on a loopback port, starts it pointed
at that port, and then sends frames and reads back events.

The compatibility `App` path can still show a cell canvas in a window.
Native code can instead use `andy.NativeScene`: a pixel-based scene with
rectangles, lines, text, rounded corners and a cursor. It is sent with
`screen.present_scene(scene)`, so native spacing and typography do not
need to be expressed as terminal characters.

```red
import "andy" as andy;
import "andy/color" as color;
import "andy/native" as native;

const screen = native.Window({"title": "Settings"});
const scene = andy.NativeScene(640, 420)
    .clear(color.rgb(0xf4, 0xf5, 0xf7))
    .text(32, 30, "Settings", color.rgb(0x20, 0x22, 0x26), 24)
    .input(32, 90, 576, 42, "Ada")
    .button(440, 160, 168, 44, "Save");

screen.run_scene(scene);
```

A second process rather than an extension because every window system
insists its event loop runs on the process's main thread, and Red's
scheduler moves a task between worker threads as it sees fit: there is
no main thread to be had inside the interpreter. Two things fall out of
that which are worth having anyway — a window that hangs cannot hang the
program, and the socket parks the task while it waits, so a window costs
no thread at all.

The protocol is lines of text and is described at the top of
[`tools/andy-gui/main.cpp`](../tools/andy-gui/main.cpp). `ANDY_TRACE`
names a file to append every frame to, which is how a disagreement
between the two halves is settled.

### Writing one

A backend is anything with these methods. There is no base class to
inherit, though [`andy/backend`](../lib/andy/backend.red) has one with
sensible answers to the questions most backends do not care about.

| Method | What it does |
|---|---|
| `start()` `stop()` | Take the screen, and give it back as it was found. |
| `size()` | The drawing area, in cells. |
| `present(canvas)` | Show a frame. |
| `poll()` | Every event since the last call. Never waits. |
| `has_mouse()` `depth()` | What it can do. |
| `set_clipboard(text)` `bell()` | Best effort. |

## Wide characters

A cell is one column. Most characters take one, a combining mark takes
none, and the characters used to write Chinese, Japanese and Korean —
along with most emoji — take two.

Everything andy draws is measured in columns, not in bytes or in
characters, so a box drawn around a name in kanji has its right edge in
the right place. A double width character occupies two cells and writing
over either half clears both, because half of a character is not a
character.

[`andy/text`](../lib/andy/text.red) is the measuring, and is usable on
its own: `width`, `truncate`, `ellipsize`, `wrap`, `pad_left`,
`pad_right`, `center`.

## Building it

Both native halves are built by the repository's CMake and need nothing
extra on macOS. On Linux, `andy-gui` needs the X11 development headers;
without them it is still built, and reports that it has no window system
rather than being missing. The terminal backend is unaffected either way.

```
$ ./setup.sh
$ ls build/andy_ext.so build/andy-gui
```
