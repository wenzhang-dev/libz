#pragma once

#include <atomic>
#include <asio/io_context.hpp>
#include <queue>

#include "executor.h"
#include "provider.h"

namespace libz {
namespace event {

class MessageLoop : public TimerProvider,
                    public DispatcherProvider,
                    public TaskProvider,
                    public ExecutorProvider,
                    public TimerWheelProvider,
                    public std::enable_shared_from_this<MessageLoop> {
 public:
  enum Type {
    kTypeDefault,
    kTypeIO,
  };

  Type type() const { return type_; }

  explicit MessageLoop(Type type);

  ~MessageLoop() override;

  static MessageLoop* Current();

 public:
  using Proactor = asio::io_context;
  virtual Proactor* proactor() { return nullptr; }
  virtual const Proactor* proactor() const { return nullptr; }

 public:
  enum State {
    kInit,
    kRunning,
    kShowdown,
  };

  State state() const { return state_; }
  void set_state(State state) { state_ = state; }
  bool IsRunning() const { return state() == kRunning; }

  virtual void Run() = 0;
  virtual void Shutdown() = 0;
  virtual void GracefulShutdown() {}

 public:
  bool IsInMessageLoopThread() const { return Current() == this; }

  void Dispatch(MessageLoop* loop, std::function<void()>&& handler) override {
    if (loop->IsInMessageLoopThread()) {
      handler();
    } else {
      loop->remote_executor()->Post(std::move(handler));
    }
  }

 public:
  class LocalExecutor : public Executor {
   public:
    LocalExecutor() = default;

    LocalExecutor(LocalExecutor&&) = default;
    LocalExecutor& operator=(LocalExecutor&&) = default;

    void Post(std::function<void()>&& handler) override {
      handlers_.push(std::move(handler));
    }

    bool empty() const { return handlers_.empty(); }
    std::size_t size() const { return handlers_.size(); }
    std::function<void()> Pop() {
      auto result = std::move(handlers_.front());
      handlers_.pop();
      return result;
    }

   private:
    std::queue<std::function<void()>> handlers_;
  };
  void Post(std::function<void()>&& handler,
            Severity severity = Severity::kNormal) override {
    switch (severity) {
      case Severity::kUrgent:
        urgent_.Post(std::move(handler));
        break;
      case Severity::kCritical:
        critical_.Post(std::move(handler));
        break;
      case Severity::kNormal:
        normal_.Post(std::move(handler));
        break;
    }
  }

 public:
  Ts WallNow() { return SystemClock::now(); }
  Tm MonoNow() { return MonotonicClock::now(); }
  std::int64_t NowUnix() {
    return DurationCast<MilliSeconds>(WallNow().time_since_epoch()).count();
  }

 public:
  void RunAt(std::function<void(Error&&)>&& handler, Tm tm) override {}

  void RunAfter(std::function<void(Error&&)>&& handler,
                MilliSeconds delay) override {}

 public:
  TimerToken AddTimerEvent(std::function<void(Error&&)>&& handler,
                           Ts ts) override {
    return {};
  }

  TimerToken AddTimerEvent(std::function<void(Error&&)>&& handler,
                           MilliSeconds delay) override {
    return {};
  }

 public:
  Executor* executor() override { return &normal_; }
  virtual Executor* remote_executor() = 0;

 protected:
  void RunOneTask(std::function<void()>&& handler) {
    handler();
  }

  void RunTasks() {
    LocalExecutor* executors[3]{&urgent_, &critical_, &normal_};

    std::vector<std::function<void()>> tasks;
    tasks.reserve(urgent_.size() + critical_.size() + normal_.size());
    for (auto* executor : executors) {
      while (!executor->empty()) {
        tasks.emplace_back(executor->Pop());
      }
    }

    for (auto& task : tasks) {
      RunOneTask(std::move(task));
    }
  }

 protected:
  Type type_;
  State state_;

  LocalExecutor urgent_;
  LocalExecutor critical_;
  LocalExecutor normal_;

  DISALLOW_COPY_MOVE_AND_ASSIGN(MessageLoop);
};

}  // namespace event
}  // namespace libz
