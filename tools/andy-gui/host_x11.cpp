// The X11 window, for Linux and the other systems with an X server.
//
// Core X11 and nothing else: no toolkit, no Xft, no fontconfig. A core
// font is loaded by name, the grid is drawn into a pixmap and the pixmap
// is copied to the window, which is both faster than drawing to the
// window directly and free of the flicker that comes of doing it the
// other way.
//
// $ANDY_X11_FONT names the font. The default is a fixed pitch font at a
// size most servers have; a system with none of them falls back to
// "fixed", which every X server has had since 1987.

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stdlib.h>
#include <string.h>

#include <cmath>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "host.h"

namespace andy {
namespace {

const uint32_t kDefaultFg = 0xd8dce4u;
const uint32_t kDefaultBg = 0x16181du;

// The fonts to try, in order. The first two are the ones a modern
// desktop has; "fixed" is the last resort and is always there.
const char* const kFonts[] = {
    "-*-dejavu sans mono-medium-r-*-*-15-*-*-*-*-*-iso10646-1",
    "-*-liberation mono-medium-r-*-*-15-*-*-*-*-*-iso10646-1",
    "-misc-fixed-medium-r-normal-*-15-*-*-*-*-*-iso10646-1",
    "-misc-fixed-medium-r-normal-*-13-*-*-*-*-*-iso8859-1",
    "9x15",
    "fixed",
    nullptr,
};

class X11Host : public Host {
 public:
  bool open(const std::string& title, int cols, int rows) override {
    display_ = XOpenDisplay(nullptr);
    if (display_ == nullptr) return false;
    screen_ = DefaultScreen(display_);

    if (!load_font()) {
      XCloseDisplay(display_);
      display_ = nullptr;
      return false;
    }

    cols_ = cols;
    rows_ = rows;
    grid_.resize(cols, rows);

    const unsigned int width = cols_ * cell_width_;
    const unsigned int height = rows_ * cell_height_;
    window_ = XCreateSimpleWindow(display_, RootWindow(display_, screen_), 0, 0,
                                  width, height, 0,
                                  pixel_for(kDefaultFg),
                                  pixel_for(kDefaultBg));
    XSelectInput(display_, window_,
                 ExposureMask | KeyPressMask | ButtonPressMask |
                     ButtonReleaseMask | PointerMotionMask |
                     StructureNotifyMask | FocusChangeMask);
    XStoreName(display_, window_, title.c_str());

    // Ask the window manager to tell us rather than closing the window
    // itself, so that the program decides whether it is finished.
    delete_atom_ = XInternAtom(display_, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display_, window_, &delete_atom_, 1);
    clipboard_atom_ = XInternAtom(display_, "CLIPBOARD", False);
    utf8_atom_ = XInternAtom(display_, "UTF8_STRING", False);
    targets_atom_ = XInternAtom(display_, "TARGETS", False);

    // Keep a useful minimum, but let native pixel scenes and resized windows
    // use arbitrary dimensions.
    XSizeHints* hints = XAllocSizeHints();
    if (hints != nullptr) {
      hints->flags = PMinSize | PBaseSize;
      hints->min_width = cell_width_ * 20;
      hints->min_height = cell_height_ * 5;
      hints->base_width = 0;
      hints->base_height = 0;
      XSetWMNormalHints(display_, window_, hints);
      XFree(hints);
    }

    gc_ = XCreateGC(display_, window_, 0, nullptr);
    XSetFont(display_, gc_, font_->fid);
    make_pixmap(width, height);

    XMapWindow(display_, window_);
    XFlush(display_);
    return true;
  }

  void present(const Grid& grid) override {
    scene_.commands.clear();
    scene_active_ = false;
    grid_ = grid;
    draw();
  }

  void present(const PixelScene& scene) override {
    scene_ = scene;
    scene_active_ = true;
    draw();
  }

  void set_pixel_size(int width, int height) override {
    if (display_ == nullptr || width <= 0 || height <= 0) return;
    XResizeWindow(display_, window_, static_cast<unsigned int>(width),
                  static_cast<unsigned int>(height));
    XFlush(display_);
  }

  void pixel_size(int* width, int* height) override {
    *width = static_cast<int>(pixmap_width_);
    *height = static_cast<int>(pixmap_height_);
  }

  void set_pixel_min(int width, int height) override {
    if (display_ == nullptr || width <= 0 || height <= 0) return;
    XSizeHints* hints = XAllocSizeHints();
    if (hints == nullptr) return;
    hints->flags = PMinSize;
    hints->min_width = width;
    hints->min_height = height;
    XSetWMNormalHints(display_, window_, hints);
    XFree(hints);
    XFlush(display_);
  }

  bool pump(std::vector<std::string>* out, int wait_ms) override {
    if (display_ == nullptr) return false;

    // Wait on the connection rather than spinning on XPending, so the
    // process is asleep whenever nothing is happening.
    if (XPending(display_) == 0) {
      struct timeval timeout;
      timeout.tv_sec = wait_ms / 1000;
      timeout.tv_usec = (wait_ms % 1000) * 1000;
      fd_set readable;
      FD_ZERO(&readable);
      const int fd = ConnectionNumber(display_);
      FD_SET(fd, &readable);
      select(fd + 1, &readable, nullptr, nullptr, &timeout);
    }

    while (XPending(display_) > 0) {
      XEvent event;
      XNextEvent(display_, &event);
      handle(event, out);
      if (closed_) return false;
    }
    return true;
  }

  void close() override {
    if (display_ == nullptr) return;
    if (pixmap_ != 0) XFreePixmap(display_, pixmap_);
    if (gc_ != nullptr) XFreeGC(display_, gc_);
    if (font_ != nullptr) XFreeFont(display_, font_);
    if (window_ != 0) XDestroyWindow(display_, window_);
    XCloseDisplay(display_);
    display_ = nullptr;
  }

  void bell() override {
    if (display_ != nullptr) XBell(display_, 0);
  }

  void set_title(const std::string& title) override {
    if (display_ != nullptr) XStoreName(display_, window_, title.c_str());
  }

  // Takes ownership of the clipboard and serves the text when somebody
  // asks for it, which is how X11 does copying: there is no clipboard,
  // only a promise to answer.
  void set_clipboard(const std::string& text) override {
    if (display_ == nullptr) return;
    clipboard_ = text;
    XSetSelectionOwner(display_, clipboard_atom_, window_, CurrentTime);
  }

  void size(int* cols, int* rows) override {
    *cols = cols_;
    *rows = rows_;
  }

 private:
  bool load_font() {
    const char* wanted = getenv("ANDY_X11_FONT");
    if (wanted != nullptr) {
      font_ = XLoadQueryFont(display_, wanted);
    }
    for (int i = 0; font_ == nullptr && kFonts[i] != nullptr; i++) {
      font_ = XLoadQueryFont(display_, kFonts[i]);
    }
    if (font_ == nullptr) return false;
    cell_width_ = font_->max_bounds.width;
    if (cell_width_ <= 0) cell_width_ = 8;
    cell_height_ = font_->ascent + font_->descent;
    if (cell_height_ <= 0) cell_height_ = 15;
    ascent_ = font_->ascent;
    return true;
  }

  // X11 wants a pixel value rather than a colour, and allocating one
  // costs a round trip, so each colour is asked about once.
  unsigned long pixel_for(uint32_t rgb) {
    std::map<uint32_t, unsigned long>::iterator found = pixels_.find(rgb);
    if (found != pixels_.end()) return found->second;
    XColor colour;
    colour.red = static_cast<unsigned short>(((rgb >> 16) & 0xff) * 257);
    colour.green = static_cast<unsigned short>(((rgb >> 8) & 0xff) * 257);
    colour.blue = static_cast<unsigned short>((rgb & 0xff) * 257);
    colour.flags = DoRed | DoGreen | DoBlue;
    Colormap map = DefaultColormap(display_, screen_);
    unsigned long pixel = BlackPixel(display_, screen_);
    if (XAllocColor(display_, map, &colour) != 0) pixel = colour.pixel;
    pixels_[rgb] = pixel;
    return pixel;
  }

  void make_pixmap(unsigned int width, unsigned int height) {
    if (pixmap_ != 0) XFreePixmap(display_, pixmap_);
    pixmap_ = XCreatePixmap(display_, window_, width, height,
                            DefaultDepth(display_, screen_));
    pixmap_width_ = width;
    pixmap_height_ = height;
  }

  // Draws the whole grid into the pixmap and copies it over in one go.
  void draw() {
    if (display_ == nullptr || pixmap_ == 0) return;

    XSetForeground(display_, gc_, pixel_for(kDefaultBg));
    XFillRectangle(display_, pixmap_, gc_, 0, 0, pixmap_width_,
                   pixmap_height_);

    if (scene_active_) {
      draw_pixel_scene();
      XCopyArea(display_, pixmap_, window_, gc_, 0, 0, pixmap_width_,
                pixmap_height_, 0, 0);
      XFlush(display_);
      return;
    }

    for (int y = 0; y < grid_.rows; y++) {
      int x = 0;
      while (x < grid_.cols) {
        Cell* first = grid_.at(x, y);
        if (first == nullptr) break;
        int end = x + 1;
        while (end < grid_.cols) {
          Cell* next = grid_.at(end, y);
          if (next == nullptr) break;
          if (next->fg != first->fg || next->bg != first->bg ||
              next->attr != first->attr) {
            break;
          }
          end++;
        }

        uint32_t fg = first->fg == kDefaultColor ? kDefaultFg : first->fg;
        uint32_t bg = first->bg == kDefaultColor ? kDefaultBg : first->bg;
        if ((first->attr & kReverse) != 0) {
          uint32_t swap = fg;
          fg = bg;
          bg = swap;
        }
        if ((first->attr & kDim) != 0) {
          // No alpha in core X11, so dimming is done by mixing towards
          // the background, which is what it looks like anyway.
          fg = mix(fg, bg, 0.45);
        }

        const double cell_width = static_cast<double>(pixmap_width_) /
                                  std::max(1, grid_.cols);
        const double cell_height = static_cast<double>(pixmap_height_) /
                                   std::max(1, grid_.rows);
        const int px = static_cast<int>(std::lround(x * cell_width));
        const int py = static_cast<int>(std::lround(y * cell_height));
        const int px_end = static_cast<int>(std::lround(end * cell_width));
        const int py_end = static_cast<int>(std::lround((y + 1) * cell_height));
        XSetForeground(display_, gc_, pixel_for(bg));
        XFillRectangle(display_, pixmap_, gc_, px, py,
                       std::max(1, px_end - px), std::max(1, py_end - py));

        XSetForeground(display_, gc_, pixel_for(fg));
        // One cell at a time, so the grid stays a grid whatever the font
        // thinks the advance should be.
        int column = x;
        for (int i = x; i < end; i++) {
          Cell* cell = grid_.at(i, y);
          if (cell->width == 0) continue;
          if (!cell->text.empty() && cell->text != " ") {
            draw_utf8(static_cast<int>(std::lround(column * cell_width)),
                      py + std::max(0, static_cast<int>(std::lround(
                          (cell_height - (font_->ascent + font_->descent)) / 2))) +
                          ascent_, cell->text);
          }
          column += (cell->width >= 2) ? 2 : 1;
        }
        if ((first->attr & kUnderline) != 0) {
          XDrawLine(display_, pixmap_, gc_, px, py_end - 1, px_end - 1,
                    py_end - 1);
        }
        if ((first->attr & kStrike) != 0) {
          const int middle = py + (py_end - py) / 2;
          XDrawLine(display_, pixmap_, gc_, px, middle, px_end - 1, middle);
        }
        x = end;
      }
    }

    if (grid_.cursor_on) {
      const double cell_width = static_cast<double>(pixmap_width_) /
                                std::max(1, grid_.cols);
      const double cell_height = static_cast<double>(pixmap_height_) /
                                 std::max(1, grid_.rows);
      const int x = static_cast<int>(std::lround(grid_.cursor_x * cell_width));
      const int y = static_cast<int>(std::lround(grid_.cursor_y * cell_height));
      const int x_end = static_cast<int>(std::lround((grid_.cursor_x + 1) * cell_width));
      const int y_end = static_cast<int>(std::lround((grid_.cursor_y + 1) * cell_height));
      XSetForeground(display_, gc_, pixel_for(kDefaultFg));
      XFillRectangle(display_, pixmap_, gc_, x, y, std::max(1, x_end - x),
                     std::max(1, y_end - y));
      Cell* under = grid_.at(grid_.cursor_x, grid_.cursor_y);
      if (under != nullptr && !under->text.empty()) {
        XSetForeground(display_, gc_, pixel_for(kDefaultBg));
        draw_utf8(x, y + ascent_, under->text);
      }
    }

    XCopyArea(display_, pixmap_, window_, gc_, 0, 0, pixmap_width_,
              pixmap_height_, 0, 0);
    XFlush(display_);
  }

  // A core font is indexed by two byte character, so UTF-8 is decoded
  // and drawn as XChar2b. Anything above the basic plane has no glyph in
  // a core font and is drawn as a replacement character rather than as
  // nothing, so that a missing glyph is visible instead of invisible.
  void draw_utf8(int x, int y, const std::string& text) {
    std::vector<XChar2b> glyphs;
    size_t i = 0;
    while (i < text.size()) {
      unsigned char lead = static_cast<unsigned char>(text[i]);
      size_t length = 1;
      uint32_t code = lead;
      if (lead >= 0xf0) {
        length = 4;
        code = lead & 0x07u;
      } else if (lead >= 0xe0) {
        length = 3;
        code = lead & 0x0fu;
      } else if (lead >= 0xc0) {
        length = 2;
        code = lead & 0x1fu;
      }
      if (i + length > text.size()) break;
      for (size_t n = 1; n < length; n++) {
        code = (code << 6) | (static_cast<unsigned char>(text[i + n]) & 0x3fu);
      }
      i += length;
      if (code > 0xffff) code = 0xfffd;
      XChar2b glyph;
      glyph.byte1 = static_cast<unsigned char>(code >> 8);
      glyph.byte2 = static_cast<unsigned char>(code & 0xff);
      glyphs.push_back(glyph);
      // Only the first cluster is drawn: a combining mark has no glyph
      // of its own in a core font and would be drawn as a box.
      break;
    }
    if (glyphs.empty()) return;
    XDrawString16(display_, pixmap_, gc_, x, y, &glyphs[0],
                  static_cast<int>(glyphs.size()));
  }

  void draw_pixel_scene() {
    for (const PixelCommand& command : scene_.commands) {
      const int x = static_cast<int>(std::lround(command.x));
      const int y = static_cast<int>(std::lround(command.y));
      const int width = std::max(1, static_cast<int>(std::lround(command.width)));
      const int height = std::max(1, static_cast<int>(std::lround(command.height)));
      const int bottom = static_cast<int>(pixmap_height_) - y - height;
      switch (command.kind) {
        case kFill:
          XSetForeground(display_, gc_, pixel_for(command.background == kDefaultColor
                                                       ? kDefaultBg : command.background));
          XFillRectangle(display_, pixmap_, gc_, x, bottom, width, height);
          break;
        case kRect:
          if (command.background != kDefaultColor) {
            XSetForeground(display_, gc_, pixel_for(command.background));
            XFillRectangle(display_, pixmap_, gc_, x, bottom, width, height);
          }
          if (command.foreground != kDefaultColor) {
            XSetForeground(display_, gc_, pixel_for(command.foreground));
            XDrawRectangle(display_, pixmap_, gc_, x, bottom, width - 1,
                           height - 1);
          }
          break;
        case kLine:
          XSetForeground(display_, gc_, pixel_for(command.foreground == kDefaultColor
                                                       ? kDefaultFg : command.foreground));
          XDrawLine(display_, pixmap_, gc_, static_cast<int>(std::lround(command.x)),
                    static_cast<int>(pixmap_height_ - std::lround(command.y)),
                    static_cast<int>(std::lround(command.x2)),
                    static_cast<int>(pixmap_height_ - std::lround(command.y2)));
          break;
        case kText:
          XSetForeground(display_, gc_, pixel_for(command.foreground == kDefaultColor
                                                       ? kDefaultFg : command.foreground));
          draw_utf8(x, bottom + ascent_, command.text);
          break;
        case kCursor:
          XSetForeground(display_, gc_, pixel_for(command.foreground == kDefaultColor
                                                       ? kDefaultFg : command.foreground));
          XFillRectangle(display_, pixmap_, gc_, x, bottom, width, height);
          break;
        case kImage:
          // Core X11 has no image decoder; the space is outlined instead.
          XSetForeground(display_, gc_, pixel_for(kDefaultFg));
          XDrawRectangle(display_, pixmap_, gc_, x, bottom, width - 1, height - 1);
          break;
      }
    }
  }

  static uint32_t mix(uint32_t a, uint32_t b, double amount) {
    const double t = amount;
    uint32_t out = 0;
    for (int shift = 16; shift >= 0; shift -= 8) {
      const double from = (a >> shift) & 0xff;
      const double to = (b >> shift) & 0xff;
      const uint32_t value = static_cast<uint32_t>(from + (to - from) * t);
      out |= (value & 0xff) << shift;
    }
    return out;
  }

  void handle(const XEvent& event, std::vector<std::string>* out) {
    switch (event.type) {
      case Expose:
        if (event.xexpose.count == 0) draw();
        return;
      case ConfigureNotify: {
        const unsigned int width = event.xconfigure.width;
        const unsigned int height = event.xconfigure.height;
        const int cols = std::max(1, static_cast<int>(width / cell_width_));
        const int rows = std::max(1, static_cast<int>(height / cell_height_));
        if (cols != cols_ || rows != rows_) {
          cols_ = cols;
          rows_ = rows;
          grid_.resize(cols_, rows_);
        }
        if (width != pixmap_width_ || height != pixmap_height_) {
          make_pixmap(width > 0 ? width : 1, height > 0 ? height : 1);
        }
        if (scene_active_) draw();
        return;
      }
      case FocusIn:
        out->push_back("focus 1");
        return;
      case FocusOut:
        out->push_back("focus 0");
        return;
      case ClientMessage:
        if (static_cast<Atom>(event.xclient.data.l[0]) == delete_atom_) {
          out->push_back("close");
        }
        return;
      case SelectionRequest:
        answer_selection(event.xselectionrequest);
        return;
      case KeyPress:
        report_key(event, out);
        return;
      case ButtonPress:
      case ButtonRelease:
        report_button(event, out);
        return;
      case MotionNotify:
        report_motion(event, out);
        return;
      default:
        return;
    }
  }

  void answer_selection(const XSelectionRequestEvent& request) {
    XSelectionEvent reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = SelectionNotify;
    reply.requestor = request.requestor;
    reply.selection = request.selection;
    reply.target = request.target;
    reply.time = request.time;
    reply.property = None;

    if (request.target == targets_atom_) {
      Atom offered[2] = {utf8_atom_, XA_STRING};
      XChangeProperty(display_, request.requestor, request.property, XA_ATOM,
                      32, PropModeReplace,
                      reinterpret_cast<unsigned char*>(offered), 2);
      reply.property = request.property;
    } else if (request.target == utf8_atom_ || request.target == XA_STRING) {
      XChangeProperty(
          display_, request.requestor, request.property, request.target, 8,
          PropModeReplace,
          reinterpret_cast<const unsigned char*>(clipboard_.data()),
          static_cast<int>(clipboard_.size()));
      reply.property = request.property;
    }
    XSendEvent(display_, request.requestor, False, 0,
               reinterpret_cast<XEvent*>(&reply));
  }

  void report_key(const XEvent& event, std::vector<std::string>* out) {
    XKeyEvent key = event.xkey;
    char buffer[32];
    KeySym symbol = 0;
    // Look the symbol up without the modifiers, so that the name of the
    // key is the key and shift only shows up in what it typed.
    const unsigned int state = key.state;
    key.state = state & ~(ShiftMask | ControlMask | Mod1Mask);
    const int length =
        XLookupString(&key, buffer, sizeof(buffer) - 1, &symbol, nullptr);
    buffer[length > 0 ? length : 0] = '\0';

    const int ctrl = (state & ControlMask) != 0 ? 1 : 0;
    const int alt = (state & Mod1Mask) != 0 ? 1 : 0;
    const int shift = (state & ShiftMask) != 0 ? 1 : 0;

    std::string name = name_for(symbol);
    std::string typed;
    if (name.empty()) {
      if (length <= 0) return;
      name = std::string(buffer, static_cast<size_t>(length));
      if (ctrl == 0 && alt == 0) {
        // What it really types, which is where shift and any input
        // method come in.
        XKeyEvent typing = event.xkey;
        char produced[32];
        KeySym ignored = 0;
        const int n = XLookupString(&typing, produced, sizeof(produced) - 1,
                                    &ignored, nullptr);
        if (n > 0) {
          typed = std::string(produced, static_cast<size_t>(n));
          name = typed;
        }
      }
    } else if (name == "space") {
      typed = " ";
    }

    out->push_back("key " + name + " " + std::to_string(ctrl) + " " +
                   std::to_string(alt) + " " + std::to_string(shift) + " " +
                   typed);
  }

  static std::string name_for(KeySym symbol) {
    switch (symbol) {
      case XK_Up: return "up";
      case XK_Down: return "down";
      case XK_Left: return "left";
      case XK_Right: return "right";
      case XK_Home: return "home";
      case XK_End: return "end";
      case XK_Prior: return "pageup";
      case XK_Next: return "pagedown";
      case XK_Insert: return "insert";
      case XK_Delete: return "delete";
      case XK_BackSpace: return "backspace";
      case XK_Return: return "enter";
      case XK_KP_Enter: return "enter";
      case XK_Tab: return "tab";
      case XK_ISO_Left_Tab: return "tab";
      case XK_Escape: return "escape";
      case XK_space: return "space";
      case XK_F1: return "f1";
      case XK_F2: return "f2";
      case XK_F3: return "f3";
      case XK_F4: return "f4";
      case XK_F5: return "f5";
      case XK_F6: return "f6";
      case XK_F7: return "f7";
      case XK_F8: return "f8";
      case XK_F9: return "f9";
      case XK_F10: return "f10";
      case XK_F11: return "f11";
      case XK_F12: return "f12";
      default: return "";
    }
  }

  void report_button(const XEvent& event, std::vector<std::string>* out) {
    const XButtonEvent& button = event.xbutton;
    const int x = scene_active_ ? button.x : column_at(button.x);
    const int y = scene_active_ ? button.y : row_at(button.y);
    const int ctrl = (button.state & ControlMask) != 0 ? 1 : 0;
    const int alt = (button.state & Mod1Mask) != 0 ? 1 : 0;
    const int shift = (button.state & ShiftMask) != 0 ? 1 : 0;

    // Buttons four and five are the wheel, which X11 reports as clicks.
    if (button.button == 4 || button.button == 5) {
      if (event.type != ButtonPress) return;
      const int wheel = button.button == 4 ? -1 : 1;
      out->push_back("mouse wheel " + std::to_string(x) + " " +
                     std::to_string(y) + " 0 " + std::to_string(wheel) + " " +
                     std::to_string(ctrl) + " " + std::to_string(alt) + " " +
                     std::to_string(shift));
      return;
    }
    int which = 1;
    if (button.button == Button2) which = 2;
    if (button.button == Button3) which = 3;
    const char* action = event.type == ButtonPress ? "press" : "release";
    if (event.type == ButtonPress) buttons_ |= 1 << which;
    else buttons_ &= ~(1 << which);
    out->push_back(std::string("mouse ") + action + " " + std::to_string(x) +
                   " " + std::to_string(y) + " " + std::to_string(which) +
                   " 0 " + std::to_string(ctrl) + " " + std::to_string(alt) +
                   " " + std::to_string(shift));
  }

  void report_motion(const XEvent& event, std::vector<std::string>* out) {
    const XMotionEvent& motion = event.xmotion;
    const int x = scene_active_ ? motion.x : column_at(motion.x);
    const int y = scene_active_ ? motion.y : row_at(motion.y);
    // A cell surface only needs moves when it crosses a cell. Pixel scenes
    // receive every native move so a hover target can track the pointer.
    if (x == pointer_x_ && y == pointer_y_) return;
    pointer_x_ = x;
    pointer_y_ = y;
    const int ctrl = (motion.state & ControlMask) != 0 ? 1 : 0;
    const int alt = (motion.state & Mod1Mask) != 0 ? 1 : 0;
    const int shift = (motion.state & ShiftMask) != 0 ? 1 : 0;
    int which = 0;
    const char* action = "move";
    if (buttons_ != 0) {
      action = "drag";
      which = (buttons_ & 2) != 0 ? 1 : ((buttons_ & 4) != 0 ? 2 : 3);
    }
    out->push_back(std::string("mouse ") + action + " " + std::to_string(x) +
                   " " + std::to_string(y) + " " + std::to_string(which) +
                   " 0 " + std::to_string(ctrl) + " " + std::to_string(alt) +
                   " " + std::to_string(shift));
  }

  int column_at(int x) const {
    if (cols_ <= 0 || pixmap_width_ == 0) return 0;
    return std::max(0, std::min(cols_ - 1,
        static_cast<int>(x * static_cast<double>(cols_) / pixmap_width_)));
  }

  int row_at(int y) const {
    if (rows_ <= 0 || pixmap_height_ == 0) return 0;
    return std::max(0, std::min(rows_ - 1,
        static_cast<int>(y * static_cast<double>(rows_) / pixmap_height_)));
  }

  Display* display_ = nullptr;
  int screen_ = 0;
  Window window_ = 0;
  Pixmap pixmap_ = 0;
  unsigned int pixmap_width_ = 0;
  unsigned int pixmap_height_ = 0;
  GC gc_ = nullptr;
  XFontStruct* font_ = nullptr;
  Atom delete_atom_ = 0;
  Atom clipboard_atom_ = 0;
  Atom utf8_atom_ = 0;
  Atom targets_atom_ = 0;
  std::string clipboard_;
  std::map<uint32_t, unsigned long> pixels_;
  Grid grid_;
  PixelScene scene_;
  int cols_ = 80;
  int rows_ = 24;
  int cell_width_ = 8;
  int cell_height_ = 15;
  int ascent_ = 12;
  int pointer_x_ = -1;
  int pointer_y_ = -1;
  int buttons_ = 0;
  bool closed_ = false;
  bool scene_active_ = false;
};

}  // namespace

Host* make_host() { return new X11Host(); }

const char* host_name() { return "x11"; }

}  // namespace andy
