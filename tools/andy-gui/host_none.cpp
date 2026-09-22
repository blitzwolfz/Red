// No window system was found when this was built.
//
// andy-gui is still built, and still runs, so that the failure is a
// message rather than a missing file: lib/andy/native.red asks the
// program what it can do before using it, and a program that says
// "nothing" is easier to act on than one that is not there.

#include "host.h"

namespace andy {

Host* make_host() { return nullptr; }

const char* host_name() { return "none"; }

}  // namespace andy
