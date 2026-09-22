// What a window has to be able to do, for andy-gui.
//
// andy draws a grid of cells. A window shows one, reports what the user
// did to it, and says when it has been closed. Everything above this
// line — the widgets, the layout, the colours — is Red, and runs
// unchanged whether the grid ends up in a terminal or here.
//
// There is one implementation per window system: host_darwin.mm for
// Cocoa and host_x11.cpp for X11. Which one is built is decided by
// CMake, and a build with neither produces a program that says so and
// exits, rather than one that is missing.

#ifndef ANDY_GUI_HOST_H
#define ANDY_GUI_HOST_H

#include <cstdint>
#include <string>
#include <vector>

namespace andy {

// The attribute bits, the same numbers lib/andy/style.red uses.
enum Attr : uint8_t {
  kBold = 1,
  kDim = 2,
  kItalic = 4,
  kUnderline = 8,
  kBlink = 16,
  kReverse = 32,
  kStrike = 64,
};

// A colour as 0xRRGGBB, or kDefaultColor for "whatever the window's own
// foreground or background is".
const uint32_t kDefaultColor = 0xff000000u;

struct Cell {
  // The characters to draw, as UTF-8. Empty means this cell is the right
  // half of a double width character and is drawn by the left half.
  std::string text;
  uint32_t fg = kDefaultColor;
  uint32_t bg = kDefaultColor;
  uint8_t attr = 0;
  // 2 for the left half of a double width character, 1 otherwise.
  uint8_t width = 1;
};

// One frame: what every cell should look like, and where the caret is.
struct Grid {
  int cols = 0;
  int rows = 0;
  std::vector<Cell> cells;
  int cursor_x = 0;
  int cursor_y = 0;
  bool cursor_on = false;

  void resize(int c, int r) {
    cols = c;
    rows = r;
    cells.assign(static_cast<size_t>(c) * static_cast<size_t>(r), Cell{});
  }

  Cell* at(int x, int y) {
    if (x < 0 || y < 0 || x >= cols || y >= rows) return nullptr;
    return &cells[static_cast<size_t>(y) * cols + x];
  }
};

// A window showing a grid.
//
// Every method is called from the process's main thread, because that is
// where AppKit insists on being and where X11 is simplest. andy-gui is a
// separate process for exactly this reason: the interpreter's scheduler
// moves a task between threads, and a window system will not have it.
class Host {
 public:
  virtual ~Host() {}

  // Opens the window. False when there is no display to open one on.
  virtual bool open(const std::string& title, int cols, int rows) = 0;

  // Shows a frame.
  virtual void present(const Grid& grid) = 0;

  // Handles whatever the window system has queued, for at most
  // `wait_ms`, and appends a line per event to `out` in the protocol
  // andy-gui speaks. Returns false once the window has gone.
  virtual bool pump(std::vector<std::string>* out, int wait_ms) = 0;

  virtual void close() = 0;
  virtual void bell() = 0;
  virtual void set_title(const std::string& title) = 0;
  virtual void set_clipboard(const std::string& text) = 0;

  // The size of the window in cells, which changes when it is resized.
  virtual void size(int* cols, int* rows) = 0;
};

// The window system this build was made for, or nullptr.
Host* make_host();

// The name of it, for the --version output.
const char* host_name();

}  // namespace andy

#endif  // ANDY_GUI_HOST_H
