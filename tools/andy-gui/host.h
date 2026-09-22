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

#include "cell_scene.h"

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
  // Pixel scenes only: draw the text in the monospaced system font.
  kMono = 128,
};

// A pixel scene is the native renderer's input. It deliberately has no
// terminal concepts such as cells, columns, wide characters, or escape
// attributes. The Red-side native toolkit can add ordinary GUI primitives
// here without teaching the terminal backend about them.
enum PixelCommandKind : uint8_t {
  kFill = 0,
  kRect = 1,
  kLine = 2,
  kText = 3,
  kCursor = 4,
  // `text` holds a file path; the image is fitted inside the rectangle.
  kImage = 5,
};

struct PixelCommand {
  PixelCommandKind kind = kFill;
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;
  float x2 = 0;
  float y2 = 0;
  float radius = 0;
  float stroke_width = 1;
  uint32_t foreground = kDefaultColor;
  uint32_t background = kDefaultColor;
  uint8_t attr = 0;
  std::string text;
};

struct PixelScene {
  float width = 0;
  float height = 0;
  std::vector<PixelCommand> commands;
};

// A native window. It can show the compatibility grid or a pixel scene.
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

  // Shows a native pixel scene. Hosts that do not implement the optional
  // path simply keep showing the last cell frame; the native hosts in this
  // tree implement it.
  virtual void present(const PixelScene& scene) { (void)scene; }

  // Requests the native surface size for a pixel scene. The compatibility
  // cell path never calls this; a real GUI can choose its own dimensions.
  virtual void set_pixel_size(int width, int height) {
    (void)width;
    (void)height;
  }

  // The drawable area in pixels, so a pixel scene can be laid out to fill
  // it. Zero when the host cannot tell.
  virtual void pixel_size(int* width, int* height) {
    *width = 0;
    *height = 0;
  }

  // The smallest drawable area the user may resize the window to.
  virtual void set_pixel_min(int width, int height) {
    (void)width;
    (void)height;
  }

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
