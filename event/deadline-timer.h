#pragma once

#include <base/common.h>
#include <base/error.h>

#include <asio/steady_timer.hpp>
#include <list>

#include "message-loop.h"

namespace libz {
namespace event {

class DeadlineTimer {
 public:
  using Timer = asio::steady_timer;
  using Handler = std::function<void(Error&&)>;

  explicit DeadlineTimer(MessageLoop* loop) : loop_(loop), timers_() {}

  ~DeadlineTimer() {}

  DeadlineTimer(DeadlineTimer&&) = default;
  DeadlineTimer& operator=(DeadlineTimer&&) = default;

 public:
  void Cancel() {
    for (auto& t : timers_) {
      t.cancel();
    }
  }

  void AddTimer(Handler&& handler, Tm tm) {
    Timer timer(*(loop_->proactor()), tm);
    auto itr = timers_.insert(timers_.end(), std::move(timer));
    itr->async_wait([this, itr = itr, handler = std::move(handler)](
                        const asio::error_code& error) mutable {
      timers_.erase(itr);
      if (error) {
        handler(Error::MkBoostError(error.value(), error.message()));
      } else {
        handler(Error{});
      }
    });
  }

  template <class Rep, class Period>
  void AddTimer(Handler&& handler, Duration<Rep, Period> delay) {
    AddTimer(std::move(handler), loop_->MonoNow() + delay);
  }

 private:
  MessageLoop* loop_;
  std::list<Timer> timers_;

  DISALLOW_COPY_AND_ASSIGN(DeadlineTimer);
};

}  // namespace event
}  // namespace libz
