// The native half of the terminal backend in lib/andy/term.red.
//
// A terminal user interface needs three things the standard library does
// not offer, because they are not things an ordinary program wants: the
// terminal in raw mode, so that a keystroke arrives as a keystroke and
// not as a line; the window size, which nothing else asks for; and a read
// that gives back whatever has arrived and does not wait for more.
//
// Everything here works on the controlling terminal, /dev/tty, rather
// than on standard input and output, so that a program whose output is
// redirected still draws to the screen it is being watched on. When
// there is no controlling terminal it falls back to the standard streams,
// which is what makes `andy` usable inside a pipeline during testing.
//
// The reads never block. andy's event loop asks for what has arrived and
// sleeps in Red between asks, so a worker thread is never sitting inside
// a system call on the interface's behalf.

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <string>

#include "red_ffi.hpp"

namespace {

// The version both halves agree on. lib/andy/term.red refuses an
// extension that answers anything else, so the two cannot drift apart.
const char* const kVersion = "1";

int g_in = -1;            // the terminal we read from
int g_out = -1;           // the terminal we draw to
bool g_own_fd = false;    // true when we opened /dev/tty and must close it
bool g_raw = false;       // is the saved mode below in force?
struct termios g_saved;   // the mode we found, to put back

// Set by the SIGWINCH handler and cleared when Red asks. A flag and
// nothing else: everything a signal handler may touch, it may touch.
volatile sig_atomic_t g_resized = 0;
struct sigaction g_saved_winch;
bool g_hooked_winch = false;

void onWinch(int) { g_resized = 1; }

// Opens the controlling terminal, or falls back to the standard streams.
// Giving up is not an option a caller has to handle: a terminal that is
// not a terminal still reads and writes, it just cannot be put in raw
// mode, and term.red asks about that separately.
bool openTerminal() {
  if (g_in >= 0) return true;
  int fd = ::open("/dev/tty", O_RDWR | O_CLOEXEC);
  if (fd >= 0) {
    g_in = fd;
    g_out = fd;
    g_own_fd = true;
    return true;
  }
  g_in = STDIN_FILENO;
  g_out = STDOUT_FILENO;
  g_own_fd = false;
  return true;
}

void closeTerminal() {
  if (g_own_fd && g_in >= 0) ::close(g_in);
  g_in = -1;
  g_out = -1;
  g_own_fd = false;
}

// Writes the whole buffer, or gives up on an error that is not a signal.
// A partial write is normal on a terminal that is being resized or whose
// reader is slow, so the loop is the point.
bool writeAll(int fd, const char* data, size_t length) {
  size_t written = 0;
  while (written < length) {
    ssize_t n = ::write(fd, data + written, length - written);
    if (n > 0) {
      written += static_cast<size_t>(n);
      continue;
    }
    if (n < 0 && (errno == EINTR)) continue;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // The terminal's buffer is full. Wait for room rather than
      // dropping what we were asked to draw.
      struct pollfd pfd;
      pfd.fd = fd;
      pfd.events = POLLOUT;
      pfd.revents = 0;
      ::poll(&pfd, 1, 50);
      continue;
    }
    return false;
  }
  return true;
}

}  // namespace

// The contract version. term.red checks this before using anything else.
RED_FUNCTION(andy_version) {
  (void)args;
  return ctx.string(kVersion);
}

// Puts the terminal into raw mode: no line editing, no echo, no signal
// characters, and a read that returns as soon as anything is there.
//
// The mode we found is kept so that close() can put it back exactly,
// which matters because the shell that started the program did not ask
// for any of this.
RED_FUNCTION(andy_term_open) {
  (void)args;
  if (!openTerminal()) return red::ext::boolean(false);
  if (g_raw) return red::ext::boolean(true);
  if (!isatty(g_in)) return red::ext::boolean(false);

  if (tcgetattr(g_in, &g_saved) != 0) return red::ext::boolean(false);
  struct termios raw = g_saved;
  raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
  raw.c_oflag &= ~(OPOST);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cflag |= CS8;
  // Read returns whatever is there, at once, including nothing.
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(g_in, TCSAFLUSH, &raw) != 0) return red::ext::boolean(false);
  g_raw = true;

  // Watch for the window changing shape. The handler sets a flag; the
  // event loop turns it into a resize event when it next looks.
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = onWinch;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  if (sigaction(SIGWINCH, &action, &g_saved_winch) == 0) g_hooked_winch = true;
  g_resized = 0;
  return red::ext::boolean(true);
}

// Puts back the mode the terminal had. Safe to call when it was never
// changed, because the way a program ends is not always the way it
// planned to.
RED_FUNCTION(andy_term_close) {
  (void)args;
  (void)ctx;
  if (g_raw && g_in >= 0) {
    tcsetattr(g_in, TCSAFLUSH, &g_saved);
    g_raw = false;
  }
  if (g_hooked_winch) {
    sigaction(SIGWINCH, &g_saved_winch, nullptr);
    g_hooked_winch = false;
  }
  closeTerminal();
  return red::ext::boolean(true);
}

// Is there a terminal on the other end at all? False under a pipe, which
// is how andy knows to pick a different backend rather than draw escape
// codes into somebody's log file.
RED_FUNCTION(andy_term_is_tty) {
  (void)args;
  (void)ctx;
  if (!openTerminal()) return red::ext::boolean(false);
  return red::ext::boolean(isatty(g_in) && isatty(g_out));
}

// The size, as "columns,rows". Nil when the terminal will not say, which
// happens under a pipe and on some remote terminals early in a session;
// term.red then falls back to $COLUMNS and $LINES and finally to 80x24.
RED_FUNCTION(andy_term_size) {
  (void)args;
  if (!openTerminal()) return red::ext::nil();
  struct winsize size;
  if (ioctl(g_out, TIOCGWINSZ, &size) != 0) return red::ext::nil();
  if (size.ws_col == 0 || size.ws_row == 0) return red::ext::nil();
  return ctx.string(std::to_string(size.ws_col) + "," +
                    std::to_string(size.ws_row));
}

// True once for each time the window changed shape since the last ask.
// Clearing on read is what makes it a report of an event rather than of a
// state, which is what the event loop wants.
RED_FUNCTION(andy_term_resized) {
  (void)args;
  (void)ctx;
  bool changed = g_resized != 0;
  g_resized = 0;
  return red::ext::boolean(changed);
}

// Writes bytes to the terminal, unchanged. The caller has already built
// the whole frame, so this is one write for one frame and the screen
// never shows half of one.
RED_FUNCTION(andy_term_write) {
  std::string_view text;
  if (!args.string(0, &text)) {
    return ctx.fail("andy_term_write() expects a string");
  }
  if (!openTerminal()) return red::ext::boolean(false);
  return red::ext::boolean(writeAll(g_out, text.data(), text.size()));
}

// Reads what has arrived, up to 4 KB, and returns it as a string of
// bytes. An empty string means nothing was waiting.
//
// `timeout` is in milliseconds and defaults to none: the call looks and
// returns. andy passes zero and sleeps in Red between frames, so that
// waiting for a keystroke never holds a scheduler thread inside a system
// call. A positive timeout is there for a program that would rather wait
// than spin, and knows what that costs.
RED_FUNCTION(andy_term_read) {
  double timeout = 0;
  if (args.size() > 0 && !args.number(0, &timeout)) {
    return ctx.fail("andy_term_read() expects a number of milliseconds");
  }
  if (!openTerminal()) return ctx.string("");

  if (timeout > 0) {
    struct pollfd pfd;
    pfd.fd = g_in;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int ready = ::poll(&pfd, 1, static_cast<int>(timeout));
    if (ready <= 0) return ctx.string("");
  } else {
    struct pollfd pfd;
    pfd.fd = g_in;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (::poll(&pfd, 1, 0) <= 0) return ctx.string("");
  }

  char buffer[4096];
  ssize_t n = ::read(g_in, buffer, sizeof(buffer));
  if (n <= 0) return ctx.string("");
  return ctx.string(std::string_view(buffer, static_cast<size_t>(n)));
}
