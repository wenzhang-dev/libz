#pragma once

#include <base/common.h>
#include <base/timer-wheel.h>

#include <functional>

namespace libz {
namespace event {

class MessageLoop;

namespace _ {

class Cancelable {
 public:
  virtual ~Cancelable() {}
  virtual void CancelEvent() = 0;
};

class TimerEvent : public Cancelable, public TimerEventBase {
 public:
  using Callback = std::function<void(Error&&)>;
  TimerEvent(Callback&& callback)
      : Cancelable(), TimerEventBase(), callback_(std::move(callback)) {}

  TimerEvent(TimerEvent&&) = default;
  TimerEvent& operator=(TimerEvent&&) = default;

  ~TimerEvent() override { callback_.reset(); }

  void OnCancel(Error&& e) override {
    if (callback_) {
      (*callback_)(std::move(e));
      callback_ = std::nullopt;
    }
  }

  void CancelEvent() override { Cancel(); }

  bool IsFired() const { return static_cast<bool>(!callback_); }

 private:
  void Execute() override {
    if (callback_) {
      (*callback_)(Error{});
      callback_ = std::nullopt;
    }
  }

  std::optional<Callback> callback_;
  DISALLOW_COPY_AND_ASSIGN(TimerEvent);
};

}  // namespace _

// Notes, we expect that the caller can hold this TimerToken object to keep
// timer task alive. The best way is that  unique_ptr wraps this the underlaying
// TimerEvent. But, typically, TimerToken is captured by lambda, and we expect
// it to be copyable. As a result, we have to use shared_ptr
class TimerToken {
 public:
  TimerToken() = default;
  TimerToken(std::unique_ptr<_::TimerEvent>&& event)
      : timer_event_(std::move(event)) {}

  TimerToken(TimerToken&&) = default;
  TimerToken& operator=(TimerToken&&) = default;

  void Cancel() {
    if (timer_event_) {
      timer_event_->Cancel();
      timer_event_.reset();
    }
  }

  // WARNING: the method will transfer the ownership
  std::shared_ptr<_::Cancelable> AsCancelable() {
    std::shared_ptr<_::TimerEvent> ptr(std::move(timer_event_));
    timer_event_.reset();
    return std::static_pointer_cast<_::Cancelable>(ptr);
  }

  bool IsEmpty() const { return !timer_event_; }
  bool IsFired() const { return timer_event_ && timer_event_->IsFired(); }

 private:
  std::unique_ptr<_::TimerEvent> timer_event_;

  DISALLOW_COPY_AND_ASSIGN(TimerToken);
};

class TimerWheel {
 public:
  explicit TimerWheel(MessageLoop* loop);

  TimerWheel(TimerWheel&&) = default;
  TimerWheel& operator=(TimerWheel&&) = default;

  ~TimerWheel() = default;

 public:
  TimerToken AddTimerEvent(std::function<void(Error&&)>&& handler, Ts ts);
  TimerToken AddTimerEvent(std::function<void(Error&&)>&& handler,
                           MilliSeconds delay);

  void Advance(Tick delta) { timer_wheel_.Advance(delta); }

  void Cancel(Error&& e) { timer_wheel_.Cancel(std::move(e)); }

  void Abort() { timer_wheel_.Abort(); }

 private:
  MessageLoop* loop_;
  ::libz::TimerWheel timer_wheel_;

  DISALLOW_COPY_AND_ASSIGN(TimerWheel);
};

}  // namespace event
}  // namespace libz
