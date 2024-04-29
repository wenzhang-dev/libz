#include <event/io-message-loop.h>

#include <atomic>
#include <thread>

namespace libz {
namespace ctl {

class IOThread {
 public:
  IOThread() = default;
  virtual ~IOThread() {}

  void Run() {
    thread_ = std::make_unique<std::thread>(
        [](IOThread* self) {
          event::IOMessageLoop loop;
          self->Init(&loop);
          loop.Run();
          self->Deinit();
        },
        this);
  }

  // Thread Safe
  void Shutdown() {
    if (!running_.load(std::memory_order_acquire)) {
      return;
    }
    loop_->Shutdown();
  }

  // block wait for thread to exit
  void Join() { thread_->join(); }

  event::MessageLoop* event_loop() { return loop_; }

  bool Running() const { return running_.load(std::memory_order_relaxed); }

 protected:
  virtual void Init(event::MessageLoop* loop) {
    loop_ = loop;
    running_.store(true, std::memory_order_release);
  }
  virtual void Deinit() {
    loop_ = nullptr;
    running_.store(false, std::memory_order_release);
  }

 private:
  std::atomic<bool> running_;
  event::MessageLoop* loop_;
  std::unique_ptr<std::thread> thread_;
};

class IOThreadPool {
 public:
  IOThreadPool(std::size_t size) : pool_(size) {}

  void Iterate(std::function<void()>&& handler) {
    for (auto& t : pool_) {
      auto loop = t.event_loop();
      loop->Dispatch(loop, std::function<void()>(handler));
    }
  }

  // Thread Safe
  void Shutdown() {
    for (auto& t : pool_) {
      t.Shutdown();
    }
  }

  // block wait for all threads to exit
  void JoinAll() {
    for (auto& t : pool_) {
      t.Join();
    }
  }

  void Run() {
    for (auto& t : pool_) {
      t.Run();
    }
  }

  IOThread* At(std::size_t num) {
    if (num >= pool_.size()) {
      return nullptr;
    }
    return &pool_[num];
  }
  std::size_t MaxIOThread() const { return pool_.size(); }

 private:
  std::vector<IOThread> pool_;
};

}  // namespace ctl
}  // namespace libz
