#include "sched_tls.h"

namespace red {

namespace {

thread_local void* g_fiber = nullptr;
thread_local void* g_worker = nullptr;

}  // namespace

void* schedulerCurrentFiber() { return g_fiber; }
void schedulerSetCurrentFiber(void* fiber) { g_fiber = fiber; }
void* schedulerCurrentWorker() { return g_worker; }
void schedulerSetCurrentWorker(void* worker) { g_worker = worker; }

}  // namespace red
