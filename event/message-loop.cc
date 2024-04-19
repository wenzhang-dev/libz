#include "message-loop.h"

#include <base/check.h>

namespace libz {
namespace event {

static thread_local MessageLoop* loop;

MessageLoop::MessageLoop(Type type)
    : TimerProvider(),
      DispatcherProvider(),
      ExecutorProvider(),
      TimerWheelProvider(),
      type_(type),
      state_(kInit),
      urgent_(),
      critical_(),
      normal_() {
  DCHECK(loop == nullptr);
  loop = this;
}

MessageLoop::~MessageLoop() {
  DCHECK(loop == this);
  loop = nullptr;
}

MessageLoop* MessageLoop::Current() { return loop; }

}  // namespace event
}  // namespace libz
