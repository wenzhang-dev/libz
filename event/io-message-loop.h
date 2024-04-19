#pragma once

#include <base/common.h>
#include <base/error.h>

#include <asio/io_context.hpp>
#include <asio/system_timer.hpp>

#include "deadline-timer.h"
#include "message-loop.h"
#include "timer-event.h"

namespace libz {
namespace event {

class IOMessageLoop : public MessageLoop {
 public:
  using Timer = asio::system_timer;

  static constexpr MilliSeconds kHeartbeatInterval = MilliSeconds(1);
  static constexpr MilliSeconds kTaskSchedInterval = MilliSeconds(10);

  IOMessageLoop()
      : MessageLoop(kTypeIO),
        now_(WallNow()),
        proactor_(),
        timer_wheel_(this),
        remote_executor_(this),
        heartbeat_timer_(),
        task_sched_timer_(),
        deadline_timer_(this) {
    Initialize();
  }

  ~IOMessageLoop() override {}

 public:
  TimerToken AddTimerEvent(std::function<void(Error&&)>&& handler,
                           Ts ts) override {
    return timer_wheel_.AddTimerEvent(std::move(handler), ts);
  }

  TimerToken AddTimerEvent(std::function<void(Error&&)>&& handler,
                           MilliSeconds delay) override {
    return timer_wheel_.AddTimerEvent(std::move(handler), delay);
  }

 public:
  void RunAt(std::function<void(Error&&)>&& handler, Tm tm) override {
    deadline_timer_.AddTimer(std::move(handler), tm);
  }

  void RunAfter(std::function<void(Error&&)>&& handler,
                MilliSeconds delay) override {
    deadline_timer_.AddTimer(std::move(handler), delay);
  }

 public:
  Proactor* proactor() override { return &proactor_; }
  const Proactor* proactor() const override { return &proactor_; }

 public:
  class RemoteExecutor : public Executor {
   public:
    RemoteExecutor(IOMessageLoop* loop) : loop_(loop) {}
    RemoteExecutor(RemoteExecutor&&) = default;
    RemoteExecutor& operator=(RemoteExecutor&&) = default;

    void Post(std::function<void()>&& handler) override {
      loop_->proactor_.post(std::move(handler));
    }

   private:
    IOMessageLoop* loop_;
  };

  Executor* remote_executor() override { return &remote_executor_; }

 public:
  void Initialize() {
    heartbeat_timer_.emplace(proactor_, WallNow() + kHeartbeatInterval);
    heartbeat_timer_->async_wait(
        [this, timer = &*heartbeat_timer_,
         cb = std::bind(&IOMessageLoop::OnHeartbeat, this)](
            const asio::error_code& error) mutable {
          SetTimer(timer, kHeartbeatInterval, std::move(cb), error);
        });

    task_sched_timer_.emplace(proactor_, WallNow() + kTaskSchedInterval);
    task_sched_timer_->async_wait(
        [this, timer = &*task_sched_timer_,
         cb = std::bind(&IOMessageLoop::OnTaskSched, this)](
            const asio::error_code& error) mutable {
          SetTimer(timer, kTaskSchedInterval, std::move(cb), error);
        });
  }

 public:
  void Run() override {
    if (state() == kInit) {
      set_state(kRunning);
      proactor_.run();
    }
  }

  void Shutdown() override {
    Dispatch(this, [this]() {
      set_state(kShowdown);
      heartbeat_timer_->cancel();
      task_sched_timer_->cancel();
      timer_wheel_.Cancel(Err(kErrorEventLoopShutdown));

      proactor_.stop();

      RunTasks();
    });
  }

 private:
  void SetTimer(Timer* timer, MilliSeconds interval,
                std::function<void()>&& callback,
                const asio::error_code& error) {
    if (!error) {
      callback();
      timer->expires_at(timer->expiry() + interval);
      timer->async_wait([this, timer, interval, cb = std::move(callback)](
                            const asio::error_code& error) mutable {
        SetTimer(timer, interval, std::move(cb), error);
      });
    }
  }

  void OnHeartbeat() {
    auto now = WallNow();
    auto delta = now.time_since_epoch() - now_.time_since_epoch();
    if (delta < kHeartbeatInterval) {
      delta = kHeartbeatInterval;
    }

    timer_wheel_.Advance(DurationCast<MilliSeconds>(delta).count());

    now_ = now;
  }
  void OnTaskSched() { RunTasks(); }

 private:
  Ts now_;
  Proactor proactor_;
  TimerWheel timer_wheel_;
  RemoteExecutor remote_executor_;

  std::optional<Timer> heartbeat_timer_;
  std::optional<Timer> task_sched_timer_;

  DeadlineTimer deadline_timer_;

  DISALLOW_COPY_MOVE_AND_ASSIGN(IOMessageLoop);
};

}  // namespace event
}  // namespace libz
