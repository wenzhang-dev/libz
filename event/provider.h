#pragma once

#include <base/common.h>

#include <cstdint>
#include <functional>

#include "basic.h"
#include "executor.h"
#include "timer-event.h"

namespace libz {
namespace event {

class MessageLoop;

// the interface allows a handler to be submitted across threads
// it's thread safe
class DispatcherProvider {
 public:
  virtual void Dispatch(MessageLoop*, std::function<void()>&& handler) = 0;
  virtual ~DispatcherProvider() {}
};

// the interface allows a handler to be submitted within thread
// it's not thread safe
class TaskProvider {
 public:
  virtual void Post(std::function<void()>&& handler, Severity) = 0;
  virtual ~TaskProvider() {}
};

class ExecutorProvider {
 public:
  virtual Executor* executor() = 0;
  virtual ~ExecutorProvider() {}
};

// the interface allows a timer handler to be submitted within thread
// it's not thread safe, and usually it's used by internal system
class TimerProvider {
 public:
  virtual void RunAt(std::function<void(Error&&)>&&, Tm) = 0;
  virtual void RunAfter(std::function<void(Error&&)>&&, MilliSeconds delay) = 0;
  virtual ~TimerProvider() {}
};

// the interface allows a timer handler to be submitted within thread
// it's not thread safe, and usually it' used by application
class TimerWheelProvider {
 public:
  virtual TimerToken AddTimerEvent(std::function<void(Error&&)>&&,
                                   MilliSeconds delay) = 0;

  virtual TimerToken AddTimerEvent(std::function<void(Error&&)>&&, Ts) = 0;
  virtual ~TimerWheelProvider() {}
};

}  // namespace event
}  // namespace libz
