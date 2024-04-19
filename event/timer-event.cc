#include "timer-event.h"

#include "message-loop.h"

namespace libz {
namespace event {

TimerToken TimerWheel::AddTimerEvent(std::function<void(Error&&)>&& handler,
                                     Ts ts) {
  auto event = std::make_unique<_::TimerEvent>(std::move(handler));

  auto now = loop_->WallNow();
  auto delta = ts - now;
  if (now >= ts) {
    delta = MilliSeconds(1);
  }

  timer_wheel_.Schedule(event.get(), DurationCast<MilliSeconds>(delta).count());

  return TimerToken(std::move(event));
}

TimerToken TimerWheel::AddTimerEvent(std::function<void(Error&&)>&& handler,
                                     MilliSeconds delay) {
  auto event = std::make_unique<_::TimerEvent>(std::move(handler));

  delay = std::max(delay, MilliSeconds{1});
  timer_wheel_.Schedule(event.get(), delay.count());

  return TimerToken(std::move(event));
}

TimerWheel::TimerWheel(MessageLoop* loop)
    : loop_(loop), timer_wheel_(loop->NowUnix()) {}

}  // namespace event
}  // namespace libz
