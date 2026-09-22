// andy-gui: a window that shows what andy draws.
//
//   andy-gui --port 54321 [--title "..."] [--cols 100] [--rows 30]
//
// Nobody runs this by hand. lib/andy/native.red listens on a loopback
// port, starts this program pointed at it, and then talks to it: frames
// down, events up, one line each.
//
// It is a separate process rather than an extension because a window
// system wants its event loop on the process's main thread and will
// misbehave or crash if it does not get it, while Red's scheduler moves
// a task between worker threads as it pleases. A process of its own is
// the only place a main thread is available to be had. It also means a
// window that hangs or is killed cannot take the program with it.
//
// The protocol is lines of text. Down, from andy:
//
//   size <cols> <rows>            the grid is this big now
//   frame                         a frame begins
//   run <x> <y> <fg> <bg> <attr> <text>
//   cursor <x> <y> | cursor off
//   end                           the frame is complete: show it
//   title <text>
//   clip <text>
//   bell
//   bye
//
// Native pixel scenes, independent of the terminal grid:
//   pixel <width> <height>
//   pfill <x> <y> <w> <h> <bg>
//   prect <x> <y> <w> <h> <radius> <stroke> <fg> <bg>
//   pline <x> <y> <x2> <y2> <stroke> <fg>
//   ptext <x> <y> <size> <fg> <attr> <text>
//   pcursor <x> <y> <w> <h> <fg>
//   pixel_end
//
// Colours are decimal 0xRRGGBB, or -1 for the window's own. `text` runs
// to the end of the line and is the only field that may contain spaces.
//
// Up, to andy:
//
//   ready <cols> <rows>
//   size <cols> <rows>
//   key <name> <ctrl> <alt> <shift> <text>
//   mouse <action> <x> <y> <button> <wheel> <ctrl> <alt> <shift>
//   focus <0|1>
//   close

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "host.h"

namespace {

// Splits off the next space separated field, leaving `rest` pointing at
// what follows. The last field of a line may contain spaces and is taken
// whole, which is why this is written by hand rather than with a split.
std::string field(const std::string& line, size_t* at) {
  size_t start = *at;
  while (start < line.size() && line[start] == ' ') start++;
  size_t end = start;
  while (end < line.size() && line[end] != ' ') end++;
  *at = end;
  return line.substr(start, end - start);
}

// Everything after the one space that ends the previous field.
//
// Exactly one: the text of a run is what andy drew, and what andy drew
// is very often a row of spaces. Skipping them the way field() does
// would shift a run left by however much of it was blank, which is a
// whole class of bug that only shows up on the screen.
std::string rest_of(const std::string& line, size_t at) {
  if (at >= line.size()) return "";
  if (line[at] == ' ') at++;
  return line.substr(at);
}

long to_long(const std::string& value, long fallback = 0) {
  if (value.empty()) return fallback;
  char* end = nullptr;
  long result = strtol(value.c_str(), &end, 10);
  if (end == value.c_str()) return fallback;
  return result;
}

double to_double(const std::string& value, double fallback = 0) {
  if (value.empty()) return fallback;
  char* end = nullptr;
  double result = strtod(value.c_str(), &end);
  if (end == value.c_str()) return fallback;
  return result;
}

uint32_t to_color(const std::string& value) {
  if (value.empty() || value == "-1") return andy::kDefaultColor;
  long n = to_long(value, -1);
  if (n < 0) return andy::kDefaultColor;
  return static_cast<uint32_t>(n) & 0xffffffu;
}

// How many columns a character takes, for the cells a run occupies. The
// same ranges lib/andy/text.red uses; the two have to agree or a run
// lands in the wrong column.
int code_width(uint32_t code) {
  if (code < 32) return 0;
  if (code >= 0x7f && code < 0xa0) return 0;
  if ((code >= 0x0300 && code <= 0x036f) ||
      (code >= 0x0483 && code <= 0x0489) ||
      (code >= 0x0591 && code <= 0x05bd) ||
      (code >= 0x0610 && code <= 0x061a) ||
      (code >= 0x064b && code <= 0x065f) ||
      (code >= 0x06d6 && code <= 0x06dc) ||
      (code >= 0x0900 && code <= 0x0903) ||
      (code >= 0x093a && code <= 0x093c) ||
      (code >= 0x0941 && code <= 0x094d) ||
      (code >= 0x1ab0 && code <= 0x1aff) ||
      (code >= 0x1dc0 && code <= 0x1dff) ||
      (code >= 0x20d0 && code <= 0x20f0) ||
      (code >= 0xfe00 && code <= 0xfe0f) ||
      (code >= 0xfe20 && code <= 0xfe2f)) {
    return 0;
  }
  if ((code >= 0x1100 && code <= 0x115f) ||
      (code >= 0x2e80 && code <= 0x303e) ||
      (code >= 0x3041 && code <= 0x33ff) ||
      (code >= 0x3400 && code <= 0x4dbf) ||
      (code >= 0x4e00 && code <= 0x9fff) ||
      (code >= 0xa000 && code <= 0xa4cf) ||
      (code >= 0xac00 && code <= 0xd7a3) ||
      (code >= 0xf900 && code <= 0xfaff) ||
      (code >= 0xfe30 && code <= 0xfe6f) ||
      (code >= 0xff00 && code <= 0xff60) ||
      (code >= 0xffe0 && code <= 0xffe6) ||
      (code >= 0x1f300 && code <= 0x1f64f) ||
      (code >= 0x1f680 && code <= 0x1f6ff) ||
      (code >= 0x1f900 && code <= 0x1f9ff) ||
      (code >= 0x20000 && code <= 0x3fffd)) {
    return 2;
  }
  return 1;
}

// Splits UTF-8 into clusters, a combining mark joining the character
// before it, and reports the width of each.
void clusters(const std::string& text, std::vector<std::string>* out,
              std::vector<int>* widths) {
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
    if (i + length > text.size()) length = 1;
    for (size_t n = 1; n < length; n++) {
      code = (code << 6) | (static_cast<unsigned char>(text[i + n]) & 0x3fu);
    }
    int width = code_width(code);
    std::string piece = text.substr(i, length);
    if (width == 0 && !out->empty()) {
      out->back() += piece;
    } else {
      out->push_back(piece);
      widths->push_back(width == 0 ? 1 : width);
    }
    i += length;
  }
}

// The socket back to andy, with a buffer for a line that arrived in
// pieces.
class Link {
 public:
  bool connect_to(int port) {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) return false;
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd_, reinterpret_cast<struct sockaddr*>(&address),
                  sizeof(address)) != 0) {
      ::close(fd_);
      fd_ = -1;
      return false;
    }
    int one = 1;
    // Frames are small and latency is the whole point, so they go out
    // as they are written rather than waiting for company.
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
    return true;
  }

  bool ok() const { return fd_ >= 0; }

  // Reads whatever has arrived and appends complete lines to `lines`.
  // False once the other end has gone.
  bool read_lines(std::vector<std::string>* lines) {
    char buffer[16384];
    for (;;) {
      ssize_t n = ::read(fd_, buffer, sizeof(buffer));
      if (n > 0) {
        pending_.append(buffer, static_cast<size_t>(n));
        if (static_cast<size_t>(n) < sizeof(buffer)) break;
        continue;
      }
      if (n == 0) return false;
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      return false;
    }
    for (;;) {
      size_t cut = pending_.find('\n');
      if (cut == std::string::npos) break;
      lines->push_back(pending_.substr(0, cut));
      pending_.erase(0, cut + 1);
    }
    return true;
  }

  bool write_line(const std::string& line) {
    std::string out = line;
    out.push_back('\n');
    size_t written = 0;
    while (written < out.size()) {
      ssize_t n = ::write(fd_, out.data() + written, out.size() - written);
      if (n > 0) {
        written += static_cast<size_t>(n);
        continue;
      }
      if (n < 0 && errno == EINTR) continue;
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        struct pollfd pfd;
        pfd.fd = fd_;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        if (::poll(&pfd, 1, 200) <= 0) return false;
        continue;
      }
      return false;
    }
    return true;
  }

  int fd() const { return fd_; }

  void shut() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
  }

 private:
  int fd_ = -1;
  std::string pending_;
};

void usage() {
  fprintf(stderr,
          "andy-gui --port N [--title T] [--cols N] [--rows N]\n"
          "\n"
          "The window half of andy. Started by lib/andy/native.red; not\n"
          "meant to be run by hand.\n");
}

}  // namespace

int main(int argc, char** argv) {
  int port = 0;
  int cols = 100;
  int rows = 30;
  std::string title = "andy";

  for (int i = 1; i < argc; i++) {
    std::string flag = argv[i];
    if (flag == "--version") {
      printf("andy-gui 1 (%s)\n", andy::host_name());
      return 0;
    }
    if (flag == "--help" || flag == "-h") {
      usage();
      return 0;
    }
    if (i + 1 >= argc) {
      usage();
      return 2;
    }
    std::string value = argv[++i];
    if (flag == "--port") port = static_cast<int>(strtol(value.c_str(), nullptr, 10));
    else if (flag == "--title") title = value;
    else if (flag == "--cols") cols = static_cast<int>(strtol(value.c_str(), nullptr, 10));
    else if (flag == "--rows") rows = static_cast<int>(strtol(value.c_str(), nullptr, 10));
  }

  if (port <= 0) {
    usage();
    return 2;
  }

  // A window that goes away should not take the program down with a
  // signal when we write to a closed socket; the failed write is a
  // better way to find out.
  signal(SIGPIPE, SIG_IGN);

  andy::Host* host = andy::make_host();
  if (host == nullptr) {
    fprintf(stderr, "andy-gui: this build has no window system\n");
    return 3;
  }

  Link link;
  if (!link.connect_to(port)) {
    fprintf(stderr, "andy-gui: could not reach andy on port %d\n", port);
    return 4;
  }

  if (!host->open(title, cols, rows)) {
    fprintf(stderr, "andy-gui: could not open a window\n");
    link.write_line("close");
    return 5;
  }

  host->size(&cols, &rows);
  link.write_line("ready " + std::to_string(cols) + " " +
                  std::to_string(rows));

  andy::Grid grid;
  andy::Grid building;
  andy::PixelScene pixel_scene;
  grid.resize(cols, rows);
  building.resize(cols, rows);

  std::vector<std::string> lines;
  std::vector<std::string> events;
  bool running = true;

  while (running) {
    lines.clear();
    if (!link.read_lines(&lines)) break;

    for (size_t n = 0; n < lines.size(); n++) {
      const std::string& line = lines[n];
      size_t at = 0;
      std::string word = field(line, &at);

      if (word == "bye") {
        running = false;
        break;
      }
      if (word == "pixel") {
        pixel_scene.width = static_cast<float>(to_double(field(line, &at)));
        pixel_scene.height = static_cast<float>(to_double(field(line, &at)));
        pixel_scene.commands.clear();
        host->set_pixel_size(static_cast<int>(pixel_scene.width),
                             static_cast<int>(pixel_scene.height));
        continue;
      }
      if (word == "pfill") {
        andy::PixelCommand command;
        command.kind = andy::kFill;
        command.x = static_cast<float>(to_double(field(line, &at)));
        command.y = static_cast<float>(to_double(field(line, &at)));
        command.width = static_cast<float>(to_double(field(line, &at)));
        command.height = static_cast<float>(to_double(field(line, &at)));
        command.background = to_color(field(line, &at));
        pixel_scene.commands.push_back(command);
        continue;
      }
      if (word == "prect") {
        andy::PixelCommand command;
        command.kind = andy::kRect;
        command.x = static_cast<float>(to_double(field(line, &at)));
        command.y = static_cast<float>(to_double(field(line, &at)));
        command.width = static_cast<float>(to_double(field(line, &at)));
        command.height = static_cast<float>(to_double(field(line, &at)));
        command.radius = static_cast<float>(to_double(field(line, &at)));
        command.stroke_width = static_cast<float>(to_double(field(line, &at), 1));
        command.foreground = to_color(field(line, &at));
        command.background = to_color(field(line, &at));
        pixel_scene.commands.push_back(command);
        continue;
      }
      if (word == "pline") {
        andy::PixelCommand command;
        command.kind = andy::kLine;
        command.x = static_cast<float>(to_double(field(line, &at)));
        command.y = static_cast<float>(to_double(field(line, &at)));
        command.x2 = static_cast<float>(to_double(field(line, &at)));
        command.y2 = static_cast<float>(to_double(field(line, &at)));
        command.stroke_width = static_cast<float>(to_double(field(line, &at), 1));
        command.foreground = to_color(field(line, &at));
        pixel_scene.commands.push_back(command);
        continue;
      }
      if (word == "ptext") {
        andy::PixelCommand command;
        command.kind = andy::kText;
        command.x = static_cast<float>(to_double(field(line, &at)));
        command.y = static_cast<float>(to_double(field(line, &at)));
        command.height = static_cast<float>(to_double(field(line, &at), 14));
        command.foreground = to_color(field(line, &at));
        command.attr = static_cast<uint8_t>(to_long(field(line, &at)));
        command.text = rest_of(line, at);
        pixel_scene.commands.push_back(command);
        continue;
      }
      if (word == "pcursor") {
        andy::PixelCommand command;
        command.kind = andy::kCursor;
        command.x = static_cast<float>(to_double(field(line, &at)));
        command.y = static_cast<float>(to_double(field(line, &at)));
        command.width = static_cast<float>(to_double(field(line, &at)));
        command.height = static_cast<float>(to_double(field(line, &at)));
        command.foreground = to_color(field(line, &at));
        pixel_scene.commands.push_back(command);
        continue;
      }
      if (word == "pixel_end") {
        host->present(pixel_scene);
        continue;
      }
      if (word == "size") {
        int c = static_cast<int>(to_long(field(line, &at), cols));
        int r = static_cast<int>(to_long(field(line, &at), rows));
        building.resize(c, r);
        grid.resize(c, r);
        continue;
      }
      if (word == "frame") {
        // A frame is built away from the one on screen, so a window
        // redrawn while it arrives shows the last complete one rather
        // than half of two.
        //
        // It starts as a copy of what is on screen, not as a blank
        // grid, because andy sends only the runs that differ from the
        // frame before. Clearing here would throw away every cell it
        // did not mention, which is almost all of them.
        building = grid;
        building.cursor_on = false;
        continue;
      }
      if (word == "run") {
        int x = static_cast<int>(to_long(field(line, &at)));
        int y = static_cast<int>(to_long(field(line, &at)));
        uint32_t fg = to_color(field(line, &at));
        uint32_t bg = to_color(field(line, &at));
        uint8_t attr = static_cast<uint8_t>(to_long(field(line, &at)));
        std::string text = rest_of(line, at);

        std::vector<std::string> pieces;
        std::vector<int> widths;
        clusters(text, &pieces, &widths);
        for (size_t i = 0; i < pieces.size(); i++) {
          andy::Cell* cell = building.at(x, y);
          if (cell == nullptr) break;
          cell->text = pieces[i];
          cell->fg = fg;
          cell->bg = bg;
          cell->attr = attr;
          cell->width = static_cast<uint8_t>(widths[i]);
          if (widths[i] >= 2) {
            andy::Cell* second = building.at(x + 1, y);
            if (second != nullptr) {
              second->text.clear();
              second->fg = fg;
              second->bg = bg;
              second->attr = attr;
              second->width = 0;
            }
          }
          x += widths[i];
        }
        continue;
      }
      if (word == "cursor") {
        std::string value = field(line, &at);
        if (value == "off") {
          building.cursor_on = false;
          continue;
        }
        building.cursor_on = true;
        building.cursor_x = static_cast<int>(to_long(value));
        building.cursor_y = static_cast<int>(to_long(field(line, &at)));
        continue;
      }
      if (word == "end") {
        grid = building;
        host->present(grid);
        continue;
      }
      if (word == "title") {
        host->set_title(rest_of(line, at));
        continue;
      }
      if (word == "clip") {
        host->set_clipboard(rest_of(line, at));
        continue;
      }
      if (word == "bell") {
        host->bell();
        continue;
      }
    }
    if (!running) break;

    events.clear();
    // A short wait inside the window system rather than a spin out here:
    // the window stays responsive and the process stays asleep.
    if (!host->pump(&events, 8)) {
      link.write_line("close");
      running = false;
    }
    int now_cols = cols;
    int now_rows = rows;
    host->size(&now_cols, &now_rows);
    if (now_cols != cols || now_rows != rows) {
      cols = now_cols;
      rows = now_rows;
      grid.resize(cols, rows);
      building.resize(cols, rows);
      events.push_back("size " + std::to_string(cols) + " " +
                       std::to_string(rows));
    }
    for (size_t i = 0; i < events.size(); i++) {
      if (!link.write_line(events[i])) {
        running = false;
        break;
      }
    }
  }

  host->close();
  link.shut();
  delete host;
  return 0;
}
