#pragma once

#include <iostream>

#include "message-loop.h"
#include "promise.h"

#ifdef ENABLE_CO

#include <coroutine>

namespace libz {
namespace event {

template <typename T>
class CoroutineTrait {
 public:
  class promise_type {
   public:
    promise_type() = default;
    promise_type(promise_type&&) = delete;
    promise_type(const promise_type&) = delete;

    // Notes, when enter coroutine function scope for the first time, the
    // function `get_return_object` will be invoked to create a Promise
    // However, the Promise object disallows copy. So, we create the Promise
    // with the same underlaying state. It's a trick way, but works
    Promise<T> get_return_object() noexcept {
      promise_.SetCoroutineHandle(
          std::coroutine_handle<promise_type>::from_promise(*this));

      return Promise<T>(promise_.state());
    }

    template <typename U>
    void return_value(U&& val) noexcept {
      promise_.Resolve(std::forward<U>(val));
    }

    void return_value(Error&& e) noexcept { promise_.Reject(std::move(e)); }
    void return_value(const Error& e) noexcept { promise_.Reject(Error{e}); }

    template <typename U>
    void return_value(Result<U>&& r) noexcept {
      if (r)
        promise_.Resolve(r.PassResult());
      else
        promise_.Reject(r.PassError());
    }

    void unhandled_exception() noexcept {
      auto ex = std::current_exception();
      try {
        std::rethrow_exception(ex);
      } catch (const std::exception& e) {
        promise_.Reject(Err(kErrorCoroutineException, e.what()));
      }
    }

    std::suspend_never initial_suspend() const noexcept { return {}; }
    std::suspend_never final_suspend() const noexcept { return {}; }

   private:
    Promise<T> promise_;
  };
};

template <>
struct CoroutineTrait<void> {
  class promise_type {
   public:
    promise_type() = default;
    promise_type(promise_type&&) = delete;
    promise_type(const promise_type&) = delete;

    Notifier get_return_object() noexcept {
      ntfr_.SetCoroutineHandle(
          std::coroutine_handle<promise_type>::from_promise(*this));

      return Notifier(ntfr_.state());
    }

    // void return_void() noexcept { ntfr_.GetResolver().Resolve(); }

    void return_value(Error&& e) noexcept {
      if (e) {
        ntfr_.GetResolver().Reject(std::move(e));
      } else {
        ntfr_.GetResolver().Resolve();
      }
    }

    void return_value(const Error& e) noexcept {
      if (e) {
        ntfr_.GetResolver().Reject(Error{e});
      } else {
        ntfr_.GetResolver().Resolve();
      }
    }

    void unhandled_exception() noexcept {
      std::exception_ptr ex = ::std::current_exception();
      try {
        std::rethrow_exception(ex);
      } catch (const std::exception& e) {
        ntfr_.GetResolver().Reject(Err(kErrorCoroutineException, e.what()));
      }
    }

    std::suspend_never initial_suspend() const noexcept { return {}; }
    std::suspend_never final_suspend() const noexcept { return {}; }

   private:
    Notifier ntfr_;
  };
};

// TODO: coroutine cancellation
template <typename T>
class PromiseAwaiter {
 public:
  explicit PromiseAwaiter(Promise<T>&& promise)
      : promise_(std::move(promise)) {}

  bool await_ready() noexcept {
    return promise_.IsPending();
  }

  void await_suspend(std::coroutine_handle<> handle) noexcept {
    auto current = MessageLoop::Current();
    DCHECK(current);

    promise_.Then(
        [this, handle](Result<T>&& r) mutable {
          res_ = std::move(r);
          handle.resume();
        },
        current->executor());
  }

  auto await_resume() {
    // Notes, for the resolved/rejected promise, await_ready will return true
    // and resume the coroutine immediately
    // In this case, the result isn't initialized. So we need get the result
    // using the async executor, that is, nullptr
    if (promise_.IsPending()) {
      promise_.Then([this](Result<T>&& r) mutable { res_ = std::move(r); },
                    nullptr);
    }
    return std::move(res_);
  }

  ~PromiseAwaiter() {}

 private:
  Promise<T> promise_;
  Result<T> res_;

  DISALLOW_COPY_AND_ASSIGN(PromiseAwaiter);
};

template <>
class PromiseAwaiter<void> {
 public:
  explicit PromiseAwaiter(Notifier&& ntfr) : ntfr_(std::move(ntfr)) {}

  bool await_ready() noexcept { return ntfr_.IsPending(); }

  void await_suspend(std::coroutine_handle<> handle) noexcept {
    auto current = MessageLoop::Current();
    DCHECK(current);

    ntfr_.Then(
        [this, handle](Error&& e) mutable {
          err_ = std::move(e);
          handle.resume();
        },
        current->executor());
  }

  auto await_resume() {
    if (ntfr_.IsPending()) {
      ntfr_.Then([this](Error&& e) { err_ = std::move(e); }, nullptr);
    }

    return std::move(err_);
  }

 private:
  Notifier ntfr_;
  Error err_;
};

template <typename T>
auto operator co_await(Promise<T>&& p) noexcept {
  return PromiseAwaiter<T>(std::move(p));
}

// FIXME: pass by reference, but still dont't touch Promise
template <typename T>
auto operator co_await(Promise<T>& p) noexcept {
  return PromiseAwaiter<T>(std::move(p));
}

auto operator co_await(Notifier&& ntfr) noexcept {
  return PromiseAwaiter<void>(std::move(ntfr));
}

auto operator co_await(Notifier& ntfr) noexcept {
  return PromiseAwaiter<void>(std::move(ntfr));
}

}  // namespace event
}  // namespace libz

namespace std {

template <typename T, typename... Args>
struct coroutine_traits<::libz::event::Promise<T>, Args...>
    : ::libz::event::CoroutineTrait<T> {};

template <typename... Args>
struct coroutine_traits<::libz::event::Notifier, Args...>
    : ::libz::event::CoroutineTrait<void> {};

}  // namespace std

#endif
