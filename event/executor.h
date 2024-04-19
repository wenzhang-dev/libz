#pragma once

#include <functional>

namespace libz {
namespace event {

// the Executor provides the execution environment for function/callback. the
// underlaying implementation maybe a thread pool and so on
class Executor {
 public:
  virtual ~Executor() {}

  // run a function/callback on appropriate time
  virtual void Post(std::function<void()>&&) = 0;
};

class LocalExecutor : public Executor {
 private:
  // the function/callback is called in place
  void Post(std::function<void()>&& f) override { f(); }
};

}  // namespace event
}  // namespace libz
