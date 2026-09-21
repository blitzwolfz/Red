// TCP sockets.
//
// Every descriptor here is non-blocking, and every call that would have
// waited instead hands its fiber to the scheduler and asks the poller to
// wake it when the descriptor is ready. A server with ten thousand open
// connections is ten thousand fibers parked in read(), costing a stack
// each, rather than ten thousand operating system threads.
//
// Outside the scheduler the same calls fall back to poll(2) on the
// calling thread, so the REPL and the test runner behave identically,
// only without the sharing.
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "../sched.h"
#include "../vm.h"
#include "builtins.h"

namespace red {

namespace {

void makeNonBlocking(int fd) {
  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// True when a call said "not now" rather than "never".
bool wouldBlock(int error) {
  return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
}

// Waits for a descriptor with this task parked, so the collector never
// has to wait on one that is doing nothing. Returns false on a timeout.
bool waitOn(VM& vm, ObjSocket* socket, bool forWrite) {
  vm.park();
  bool ready = Scheduler::waitReady(socket->fd, forWrite, socket->timeout);
  vm.unpark();
  return ready;
}

Value nativeTcpListen(VM& vm, int argCount, Value* args) {
  if (!isNumber(args[0])) {
    return vm.failAs("type", "tcp_listen() expects a port number.");
  }
  int port = (int)asNumber(args[0]);
  int backlog = argCount > 1 && isNumber(args[1]) ? (int)asNumber(args[1]) : 128;

  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return vm.failAs("net", "socket() failed: %s", std::strerror(errno));
  }

  // Without this a restarted server cannot rebind while the old socket is
  // still in TIME_WAIT.
  int reuse = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_in address;
  std::memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons((uint16_t)port);

  // An explicit host, for a server that should not be reachable from
  // anywhere but this machine.
  if (argCount > 2 && isString(args[2])) {
    std::string host(asString(args[2])->chars, asString(args[2])->length);
    if (host != "0.0.0.0" && !host.empty()) {
      if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        ::close(fd);
        return vm.failAs("net", "tcp_listen() does not understand the address '%s'.",
                         host.c_str());
      }
    }
  }

  if (::bind(fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
    int saved = errno;
    ::close(fd);
    return vm.failAs("net", "bind() on port %d failed: %s", port,
                     std::strerror(saved));
  }
  if (::listen(fd, backlog) < 0) {
    int saved = errno;
    ::close(fd);
    return vm.failAs("net", "listen() failed: %s", std::strerror(saved));
  }
  makeNonBlocking(fd);
  return objValue((Obj*)vm.runtime().newSocket(fd, true));
}

Value nativeTcpConnect(VM& vm, int argCount, Value* args) {
  if (!isString(args[0]) || !isNumber(args[1])) {
    return vm.failAs("type",
                     "tcp_connect() expects a host string and a port number.");
  }
  std::string host(asString(args[0])->chars, asString(args[0])->length);
  std::string port = std::to_string((int)asNumber(args[1]));
  double timeout = argCount > 2 && isNumber(args[2]) ? asNumber(args[2]) : 0;

  struct addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  // Name resolution is the one call here that genuinely blocks its
  // thread: there is no portable non-blocking getaddrinfo. Saying so
  // lets the scheduler cover this worker while it waits.
  struct addrinfo* results = nullptr;
  int status;
  {
    Scheduler::BlockingRegion blocking;
    vm.park();
    status = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &results);
    vm.unpark();
  }
  if (status != 0) {
    return vm.failAs("net", "tcp_connect() could not resolve '%s': %s",
                     host.c_str(), ::gai_strerror(status));
  }

  int fd = -1;
  int savedErrno = 0;
  for (struct addrinfo* it = results; it != nullptr; it = it->ai_next) {
    fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (fd < 0) {
      savedErrno = errno;
      continue;
    }
    makeNonBlocking(fd);
    if (::connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
    if (errno != EINPROGRESS) {
      savedErrno = errno;
      ::close(fd);
      fd = -1;
      continue;
    }
    // In progress: the socket becomes writable when it has finished, one
    // way or the other, and SO_ERROR says which.
    vm.park();
    bool ready = Scheduler::waitReady(fd, true, timeout);
    vm.unpark();
    int failure = 0;
    socklen_t length = sizeof(failure);
    if (ready &&
        ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &failure, &length) == 0 &&
        failure == 0) {
      break;
    }
    savedErrno = ready ? failure : ETIMEDOUT;
    ::close(fd);
    fd = -1;
  }
  ::freeaddrinfo(results);

  if (fd < 0) {
    return vm.failAs("net", "tcp_connect() to %s:%s failed: %s", host.c_str(),
                     port.c_str(), std::strerror(savedErrno));
  }
  ObjSocket* socket = vm.runtime().newSocket(fd, false);
  socket->timeout = timeout;
  return objValue((Obj*)socket);
}

bool requireOpenSocket(VM& vm, ObjSocket* socket, const char* who) {
  if (socket->closed || socket->fd < 0) {
    vm.failAs("net", "%s on a closed socket.", who);
    return false;
  }
  return true;
}

Value socketAccept(VM& vm, int, Value* args) {
  ObjSocket* server = asSocket(args[0]);
  if (!requireOpenSocket(vm, server, "accept()")) return nilValue();
  if (!server->listening) {
    return vm.failAs("net", "accept() on a client socket.");
  }

  for (;;) {
    int client = ::accept(server->fd, nullptr, nullptr);
    if (client >= 0) {
      makeNonBlocking(client);
      // Small writes go out at once rather than waiting for more to
      // keep them company. A request/response protocol spends its life
      // sending one small message and waiting for the answer, which is
      // exactly what Nagle's algorithm delays.
      int on = 1;
      ::setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
      ObjSocket* socket = vm.runtime().newSocket(client, false);
      socket->timeout = server->timeout;
      return objValue((Obj*)socket);
    }
    int savedErrno = errno;
    if (!wouldBlock(savedErrno)) {
      return vm.failAs("net", "accept() failed: %s", std::strerror(savedErrno));
    }
    // Nothing waiting. Park this fiber until the listening socket has a
    // connection on it; the worker takes up something else meanwhile.
    if (!waitOn(vm, server, false)) {
      if (server->closed || server->fd < 0) {
        return vm.failAs("net", "accept() on a closed socket.");
      }
      return vm.failAs("timeout", "accept() timed out.");
    }
  }
}

Value socketRead(VM& vm, int argCount, Value* args) {
  ObjSocket* socket = asSocket(args[0]);
  if (!requireOpenSocket(vm, socket, "read()")) return nilValue();

  size_t limit = 4096;
  if (argCount > 1) {
    if (!isNumber(args[1]) || asNumber(args[1]) <= 0) {
      return vm.failAs("type", "read() expects a positive byte count.");
    }
    limit = (size_t)asNumber(args[1]);
  }

  std::string buffer;
  buffer.resize(limit);
  for (;;) {
    ssize_t got = ::recv(socket->fd, &buffer[0], limit, 0);
    if (got > 0) {
      return objValue((Obj*)vm.runtime().copyString(buffer.data(), (size_t)got));
    }
    // Zero bytes means the peer closed its side, reported as nil so a
    // read loop can stop.
    if (got == 0) return nilValue();

    int savedErrno = errno;
    if (!wouldBlock(savedErrno)) {
      return vm.failAs("net", "read() failed: %s", std::strerror(savedErrno));
    }
    if (!waitOn(vm, socket, false)) {
      return vm.failAs("timeout", "read() timed out.");
    }
  }
}

Value socketWrite(VM& vm, int argCount, Value* args) {
  ObjSocket* socket = asSocket(args[0]);
  if (!requireOpenSocket(vm, socket, "write()")) return nilValue();

  std::string text;
  for (int i = 1; i < argCount; i++) text += valueToString(args[i]);

  size_t sent = 0;
  while (sent < text.size()) {
    ssize_t wrote =
        ::send(socket->fd, text.data() + sent, text.size() - sent, 0);
    if (wrote > 0) {
      sent += (size_t)wrote;
      continue;
    }
    int savedErrno = errno;
    if (!wouldBlock(savedErrno)) {
      return vm.failAs("net", "write() failed after %zu bytes: %s", sent,
                       std::strerror(savedErrno));
    }
    if (!waitOn(vm, socket, true)) {
      return vm.failAs("timeout", "write() timed out after %zu bytes.", sent);
    }
  }
  return numberValue((double)sent);
}

Value socketClose(VM&, int, Value* args) {
  ObjSocket* socket = asSocket(args[0]);
  if (!socket->closed && socket->fd >= 0) {
    ::close(socket->fd);
    socket->closed = true;
    socket->fd = -1;
  }
  return nilValue();
}

Value socketFd(VM&, int, Value* args) {
  return numberValue((double)asSocket(args[0])->fd);
}

// How long this socket's calls may wait. Zero, the default, means as
// long as it takes.
Value socketSetTimeout(VM& vm, int, Value* args) {
  if (!isNumber(args[1]) || asNumber(args[1]) < 0) {
    return vm.failAs("type", "set_timeout() expects a number of seconds.");
  }
  asSocket(args[0])->timeout = asNumber(args[1]);
  return args[0];
}

Value socketTimeout(VM&, int, Value* args) {
  return numberValue(asSocket(args[0])->timeout);
}

Value socketIsClosed(VM&, int, Value* args) {
  ObjSocket* socket = asSocket(args[0]);
  return boolValue(socket->closed || socket->fd < 0);
}

// The port this socket is actually bound to. Listening on port 0 asks the
// system to pick a free one, and this is how a program finds out which.
Value socketPort(VM& vm, int, Value* args) {
  ObjSocket* socket = asSocket(args[0]);
  if (!requireOpenSocket(vm, socket, "port()")) return nilValue();

  struct sockaddr_in address;
  socklen_t length = sizeof(address);
  if (::getsockname(socket->fd, (struct sockaddr*)&address, &length) < 0) {
    return vm.failAs("net", "port() failed: %s", std::strerror(errno));
  }
  return numberValue((double)ntohs(address.sin_port));
}

// The address at the other end, as "1.2.3.4:5678". Empty for a socket
// with no peer.
Value socketPeer(VM& vm, int, Value* args) {
  ObjSocket* socket = asSocket(args[0]);
  if (!requireOpenSocket(vm, socket, "peer()")) return nilValue();

  struct sockaddr_in address;
  socklen_t length = sizeof(address);
  if (::getpeername(socket->fd, (struct sockaddr*)&address, &length) < 0) {
    return objValue((Obj*)vm.runtime().copyString("", 0));
  }
  char text[INET_ADDRSTRLEN];
  if (::inet_ntop(AF_INET, &address.sin_addr, text, sizeof(text)) == nullptr) {
    return objValue((Obj*)vm.runtime().copyString("", 0));
  }
  std::string result =
      std::string(text) + ":" + std::to_string(ntohs(address.sin_port));
  return objValue((Obj*)vm.runtime().copyString(result));
}

}  // namespace

void installNet(Runtime& runtime) {
  defineGlobalFn(runtime, "tcp_listen", nativeTcpListen, -1);
  defineGlobalFn(runtime, "tcp_connect", nativeTcpConnect, -1);

  defineMethodFn(runtime, ObjType::Socket, "accept", socketAccept, 1);
  defineMethodFn(runtime, ObjType::Socket, "read", socketRead, -1);
  defineMethodFn(runtime, ObjType::Socket, "write", socketWrite, -1);
  defineMethodFn(runtime, ObjType::Socket, "close", socketClose, 1);
  defineMethodFn(runtime, ObjType::Socket, "fd", socketFd, 1);
  defineMethodFn(runtime, ObjType::Socket, "port", socketPort, 1);
  defineMethodFn(runtime, ObjType::Socket, "peer", socketPeer, 1);
  defineMethodFn(runtime, ObjType::Socket, "set_timeout", socketSetTimeout, 2);
  defineMethodFn(runtime, ObjType::Socket, "timeout", socketTimeout, 1);
  defineMethodFn(runtime, ObjType::Socket, "is_closed", socketIsClosed, 1);
}

}  // namespace red
