#pragma once

#include <base/bind.h>
#include <base/common.h>
#include <base/error.h>
#include <base/result.h>
#include <base/trait.h>

#include <functional>
#include <memory>

#include "basic.h"
#include "executor.h"

#ifdef ENABLE_CO
#include <coroutine>
#endif  // ENABLE_CO

namespace libz {
namespace event {

template <typename T>
class Promise;

template <typename T>
struct IsPromise : std::false_type {};

template <typename T>
struct IsPromise<Promise<T>> : std::true_type {};

namespace _ {

enum class PromiseStatus : std::uint8_t {
  // initial state
  kInit,
  // pre-fulfilled, pending state, its callback will be invoked
  kPreFulfilled,
  // fulfilled, its callback has already been invoked in executor
  kFulfilled,
  // pre-rejected state, pending state. its callback will be invoked
  kPreRejected,
  // rejected, its callback has already been invoked in executor
  kRejected,
  // cancalled, its callback and storage has been purged
  kCancelled,
};

class PromiseStatusMachine {
 public:
  PromiseStatusMachine() = default;

  PromiseStatus status() const { return status_; }

  bool IsEmpty() const { return status() == PromiseStatus::kInit; }
  bool IsPreFulfilled() const {
    return status() == PromiseStatus::kPreFulfilled;
  }
  bool IsFulfilled() const { return status() == PromiseStatus::kFulfilled; }
  bool IsPreRejected() const { return status() == PromiseStatus::kPreRejected; }
  bool IsRejected() const { return status() == PromiseStatus::kRejected; }
  bool IsCancelled() const { return status() == PromiseStatus::kCancelled; }

 public:
  bool ToPreFulfilled() {
    return To(PromiseStatus::kInit, PromiseStatus::kPreFulfilled);
  }
  bool ToFulfilled() {
    return To(PromiseStatus::kPreFulfilled, PromiseStatus::kFulfilled);
  }
  bool ToPreRejected() {
    return To(PromiseStatus::kInit, PromiseStatus::kPreRejected);
  }
  bool ToRejected() {
    return To(PromiseStatus::kPreRejected, PromiseStatus::kRejected);
  }
  bool ToCancelled() {
    switch (status()) {
      case PromiseStatus::kInit:
      case PromiseStatus::kPreRejected:
      case PromiseStatus::kPreFulfilled:
        status_ = PromiseStatus::kCancelled;
        return true;
      default:
        return false;
    }
  }

  void Force(PromiseStatus s) { status_ = s; }

 public:
  // the callback has not been invoked
  bool IsPending() const { return IsPreRejected() || IsPreFulfilled(); }

  // the callback has been invoked in given executor
  bool IsDone() const { return IsFulfilled() || IsRejected(); }

  // the promise has invoked |resolve|
  bool IsSatisfied() const { return IsPreFulfilled() || IsFulfilled(); }

  // the promise has invoked |reject|
  bool IsUnsatisfied() const { return IsPreRejected() || IsRejected(); }

  // the result has been settled
  bool IsSettled() const { return !IsEmpty() && !IsCancelled(); }

 private:
  bool To(PromiseStatus from, PromiseStatus to) {
    if (status() == from) {
      status_ = to;
      return true;
    }
    return false;
  }

  PromiseStatus status_{PromiseStatus::kInit};
};

class PromiseStateBase : public std::enable_shared_from_this<PromiseStateBase> {
 public:
  PromiseStateBase() = default;
  virtual ~PromiseStateBase() {}

  struct Propagator {
    virtual void PropagateResult(void*) = 0;
    virtual void PropagatePromise(void*) = 0;

    virtual ~Propagator() {}
  };

 public:
  virtual Propagator* propagator() const { return nullptr; }
  virtual Propagator* next_propagator() const { return nullptr; }

  virtual PromiseStateBase* previous() const { return nullptr; }
  virtual PromiseStateBase* next() const { return nullptr; }

  // when the node goes out of the scope, detach the promise chain
  virtual void DetachFromChain() = 0;

 public:
  void Cancel() {
    for (auto c = this; c; c = c->next()) {
      if (c->ToCancelled()) {
        c->OnCancel();
      }
    }
  }
  virtual void OnCancel() {}

 public:
  virtual bool HasHandler() const = 0;
  virtual Executor* GetExecutor() const = 0;

 public:
  PromiseStatus status() const { return status_.status(); }

  bool IsDone() const { return status_.IsDone(); }
  bool IsPending() const { return status_.IsPending(); }
  bool IsSatisfied() const { return status_.IsSatisfied(); }
  bool IsUnsatisfied() const { return status_.IsUnsatisfied(); }
  bool IsSettled() const { return status_.IsSettled(); }

  bool IsEmpty() const { return status_.IsEmpty(); }
  bool IsPreFulfilled() const { return status_.IsPreFulfilled(); }
  bool IsFulfilled() const { return status_.IsFulfilled(); }
  bool IsPreRejected() const { return status_.IsPreRejected(); }
  bool IsRejected() const { return status_.IsRejected(); }
  bool IsCancelled() const { return status_.IsCancelled(); }

 public:
  bool ToPreFulfilled() { return status_.ToPreFulfilled(); }
  bool ToFulfilled() { return status_.ToFulfilled(); }
  bool ToPreRejected() { return status_.ToPreRejected(); }
  bool ToRejected() { return status_.ToRejected(); }
  bool ToCancelled() { return status_.ToCancelled(); }
  void Force(PromiseStatus s) { status_.Force(s); }

 private:
  PromiseStatusMachine status_;
};

template <typename T>
class PromiseState : public PromiseStateBase,
                     public PromiseStateBase::Propagator {
 public:
  using ValueType = T;
  using Callback = std::function<void(Result<T>&&)>;
  using Propagator = PromiseStateBase::Propagator;

  PromiseState()
      : PromiseStateBase(),
        PromiseStateBase::Propagator(),
        storage_(),
        callback_(),
        executor_(nullptr),
        previous_(),
        next_(nullptr) {}

  PromiseState(PromiseState&&) = default;
  PromiseState& operator=(PromiseState&&) = default;

  ~PromiseState() override {
    if (previous_) {
      previous_->DetachFromChain();
    }
  }

 public:
  template <typename U>
  bool Resolve(U&& value) {
    if (IsEmpty()) {
      storage_.emplace(std::forward<U>(value));
      ToPreFulfilled();
      TryInvokeCallback();
      return true;
    }
    return false;
  }

  bool Reject(Error&& e) {
    if (IsEmpty()) {
      storage_.emplace(std::move(e));
      ToPreRejected();
      TryInvokeCallback();
      return true;
    }
    return false;
  }

  void Cancel() {
    if (IsEmpty() || IsPending()) {
      callback_ = {};
      storage_ = std::nullopt;
#ifdef ENABLE_CO
      if (co_handle_) {
        co_handle_.destroy();
      }
#endif  // ENABLE_CO
      ToCancelled();
    }
  }

  template <typename U>
  void Watch(PromiseState<U>* other) {
    previous_ = other->shared_from_this();
    other->set_next(this);
  }

 public:
  Propagator* propagator() const override {
    return const_cast<Propagator*>(static_cast<const Propagator*>(this));
  }

  Propagator* next_propagator() const override {
    return next_ ? next_->propagator() : nullptr;
  }

  void set_next(PromiseStateBase* p) { next_ = p; }
  PromiseStateBase* next() const override { return next_; }
  PromiseStateBase* previous() const override { return previous_.get(); }

  void DetachFromChain() override { set_next(nullptr); }

 public:
  void PropagatePromise(void*) override;
  void PropagateResult(void* result) override {
    auto* r = reinterpret_cast<Result<T>*>(result);
    if (*r) {
      Resolve(r->PassResult());
    } else {
      Reject(r->PassError());
    }
  }

 public:
  Executor* GetExecutor() const override { return executor_; }
  bool HasHandler() const override { return static_cast<bool>(callback_); }

 public:
  template <typename F, typename RT = std::invoke_result_t<F, T>,
            std::enable_if_t<std::is_void<RT>::value, int> = 0>
  void Attach(F&& callback, Executor* executor);

  template <typename U, typename F, typename RT = std::invoke_result_t<F, T>,
            std::enable_if_t<IsResult<RT>::value, int> = 0>
  void Attach(PromiseState<U>* next, F&& callback, Executor* exectuor);

  template <typename U, typename F, typename RT = std::invoke_result_t<F, T>,
            std::enable_if_t<IsPromise<RT>::value, int> _ = 0>
  void Attach(PromiseState<U>* next, F&& callback, Executor* executor);

  template <typename F, typename RT = std::invoke_result_t<F, T>,
            std::enable_if_t<IsResult<RT>::value, int> = 0>
  void Attach(F&& callback, Executor* exectuor);

#ifdef ENABLE_CO
  void SetCoroutineHandle(std::coroutine_handle<> handle) {
    co_handle_ = handle;
  }
#endif  // ENABLE_CO

 private:
  void TryInvokeCallback() {
    if (callback_ && IsPending()) {
      auto cb = [this]() -> void {
        switch (status()) {
          case PromiseStatus::kPreFulfilled:
            CHECK(ToFulfilled());
            InvokeCallback();
            break;

          case PromiseStatus::kPreRejected:
            CHECK(ToRejected());
            InvokeCallback();
            break;

          default:
            break;
        }
      };

      RunInExecutor(BindWeak(this, std::move(cb)));
    }
  }

  void InvokeCallback() {
    DCHECK(storage_);
    auto tmp = Pass(&callback_);
    NO_EXCEPT(tmp(std::move(storage_.value())));
  }

  template <typename F>
  void RunInExecutor(F&& callback) {
    if (executor_) {
      executor_->Post(std::move(callback));
    } else {
      NO_EXCEPT(callback());
    }
  }

  void AddCallback(Callback&& cb, Executor* executor) {
    callback_ = std::move(cb);
    executor_ = executor;
    TryInvokeCallback();
  }

 private:
  std::optional<Result<T>> storage_;

  Callback callback_;
  Executor* executor_;

  // hold a strong pointer to previous promise
  // so when we hold the last promise, the whole promises are alive
  std::shared_ptr<PromiseStateBase> previous_;

  // the next promise pointer is mainly used to propagate result/promise
  PromiseStateBase* next_;

#ifdef ENABLE_CO
  std::coroutine_handle<> co_handle_;
#endif  // ENABLE_CO

  template <typename U>
  friend class Promise;

  DISALLOW_COPY_AND_ASSIGN(PromiseState);
};

// eg.
// Promise<int> p0;
// p0.Then([&](Result<int>&& r) -> void {
//    return;
// }, &executor);
template <typename T>
template <typename F, typename RT,
          std::enable_if_t<std::is_void<RT>::value, int>>
void PromiseState<T>::Attach(F&& callback, Executor* executor) {
  // Notes, when the releted promsie is destructed, which callback should be
  // ignore quitely and never be invoked
  auto cb = [f = std::forward<F>(callback),
             maybe_promise = weak_from_this()](Result<T>&& r) mutable -> void {
    if (auto promise = maybe_promise.lock(); promise) {
      // the promise return |void| cannot keep the next propagator chain
      Propagator* pp = promise->next_propagator();
      DCHECK(!pp);

      NO_EXCEPT(std::invoke(std::forward<F>(f), std::move(r)));
    }
  };

  AddCallback(std::move(cb), executor);
}

template <typename T>
template <typename F, typename RT, std::enable_if_t<IsResult<RT>::value, int>>
void PromiseState<T>::Attach(F&& callback, Executor* exectuor) {
  auto cb = [f = std::forward<F>(callback),
             maybe_promise = weak_from_this()](Result<T>&& r) mutable -> void {
    if (auto promise = maybe_promise.lock(); promise) {
      Propagator* pp = promise->next_propagator();
      auto result = NO_EXCEPT(std::invoke(std::forward<F>(f), std::move(r)));
      if (pp) {
        pp->PropagateResult(&result);
      }
    }
  };
  AddCallback(std::move(cb), exectuor);
}

// eg.
// Promise<int> p0;
// auto p1 = p0.Then([&](Result<int>&& r) -> Result<bool> {
//    return false;
// }, &exectuor);
template <typename T>
template <typename U, typename F, typename RT,
          std::enable_if_t<IsResult<RT>::value, int>>
void PromiseState<T>::Attach(PromiseState<U>* next, F&& callback,
                             Executor* executor) {
  next->Watch(this);

  // Notes, when the releted promsie is destructed, which callback should be
  // ignore quitely and never be invoked
  auto cb = [f = std::forward<F>(callback),
             maybe_promise = weak_from_this()](Result<T>&& r) mutable -> void {
    if (auto promise = maybe_promise.lock(); promise) {
      Propagator* pp = promise->next_propagator();
      auto result = NO_EXCEPT(std::invoke(std::forward<F>(f), std::move(r)));
      if (pp) {
        pp->PropagateResult(&result);
      }
    }
  };

  AddCallback(std::move(cb), executor);
}

// eg.
// Promise<int> p0;
// Promise<bool> p1 = p0.Then([&](Result<int>&& r) -> Promise<bool> {
//  return true;
// }, &exec);
template <typename T>
template <typename U, typename F, typename RT,
          std::enable_if_t<IsPromise<RT>::value, int>>
void PromiseState<T>::Attach(PromiseState<U>* next, F&& callback,
                             Executor* executor) {
  next->Watch(this);

  // Notes, when the releted promsie is destructed, which callback should be
  // ignore quitely and never be invoked
  auto cb = [f = std::forward<F>(callback),
             maybe_promise = weak_from_this()](Result<T>&& r) mutable -> void {
    if (auto promise = maybe_promise.lock(); promise) {
      Propagator* pp = promise->next_propagator();
      auto inner_promise =
          NO_EXCEPT(std::invoke(std::forward<F>(f), std::move(r)));
      if (pp) {
        pp->PropagatePromise(&inner_promise);
      }
    }
  };

  AddCallback(std::move(cb), executor);
}

template <>
class PromiseState<void> : public PromiseStateBase,
                           public PromiseStateBase::Propagator {
 public:
  using ValueType = void;
  using Propagator = PromiseStateBase::Propagator;

  PromiseState()
      : PromiseStateBase(),
        PromiseStateBase::Propagator(),
        storage_(),
        previous_(),
        next_(nullptr) {}

  PromiseState(PromiseState&&) = default;
  PromiseState& operator=(PromiseState&&) = default;

  ~PromiseState() override {
    if (previous_) {
      previous_->DetachFromChain();
    }
  }

  // Since this specialization doesn't need invoke callback in given executor,
  // it's not necessary to transfer state to |kPreFulfilled| and |kPreRejected|.
  // we should force state transition when call |Resolve| and |Reject|.
  bool Resolve() {
    if (IsEmpty()) {
      Result<void> r{};
      PropagateResult(&r);
      return true;
    }
    return false;
  }

  bool Reject(Error&& e) {
    if (IsEmpty()) {
      Result<void> r{std::move(e)};
      PropagateResult(&r);
      return true;
    }
    return false;
  }

  void Cancel() {
    if (IsEmpty() || IsPending()) {
      storage_ = std::nullopt;
#ifdef ENABLE_CO
      if (co_handle_) {
        co_handle_.destroy();
      }
#endif  // ENABLE_CO
      ToCancelled();
    }
  }

  Result<void> PassResult() {
    DCHECK(storage_);
    return Pass(&storage_);
  }

  template <typename U>
  void Watch(PromiseState<U>* other) {
    previous_ = other->shared_from_this();
    other->set_next(this);
  }

 public:
  void set_next(PromiseStateBase* p) { next_ = p; }
  PromiseStateBase* next() const override { return next_; }
  PromiseStateBase* previous() const override { return previous_.get(); }

  Propagator* propagator() const override {
    return const_cast<Propagator*>(static_cast<const Propagator*>(this));
  }

  void DetachFromChain() override { set_next(nullptr); }

  bool HasHandler() const override { return false; }
  Executor* GetExecutor() const override { return nullptr; }

 public:
  void PropagateResult(void* result) override {
    DCHECK(!IsDone());

    auto* r = reinterpret_cast<Result<void>*>(result);
    storage_ = std::move(*r);

    if (NO_EXCEPT(storage_.value())) {
      Force(PromiseStatus::kFulfilled);
    } else {
      Force(PromiseStatus::kRejected);
    }

    if (auto n = next(); n) {
      if (auto pp = n->propagator(); pp) {
        Result<void> tmp{*storage_};
        pp->PropagateResult(&tmp);
      }
    }
  }

  inline void PropagatePromise(void*) override;

#ifdef ENABLE_CO
  void SetCoroutineHandle(std::coroutine_handle<> handle) {
    co_handle_ = handle;
  }
#endif  // ENABLE_CO

 private:
  std::optional<Result<void>> storage_;

  std::shared_ptr<PromiseStateBase> previous_;
  PromiseStateBase* next_;

#ifdef ENABLE_CO
  std::coroutine_handle<> co_handle_;
#endif  // ENABLE_CO

  template <typename U>
  friend class Promise;

  DISALLOW_COPY_AND_ASSIGN(PromiseState);
};

template <typename T, typename P>
class PromiseStateAttachment : public PromiseState<T> {
 public:
  template <typename... ARGS>
  PromiseStateAttachment(ARGS&&... args)
      : PromiseState<T>(), payload_(std::forward<ARGS>(args)...) {}

  P* GetPayload() { return &payload_; }
  const P* GetPayload() const { return &payload_; }

 private:
  P payload_;
};

template <typename P>
class PromiseStateAttachment<void, P> : public PromiseState<void> {
 public:
  template <typename... ARGS>
  PromiseStateAttachment(ARGS&&... args)
      : PromiseState<void>(), payload_(std::forward<ARGS>(args)...) {}

  P* GetPayload() { return &payload_; }
  const P* GetPayload() const { return &payload_; }

 private:
  P payload_;
};

}  // namespace _

template <typename T>
class PromiseResolver {
 public:
  template <typename U>
  bool Set(Result<U>&& r) {
    if (r) {
      return Resolve(r.PassResult());
    } else {
      return Reject(r.PassError());
    }
  }

  template <typename U>
  bool Resolve(U&&);

  bool Reject(Error&&);
  void Cancel();

  void Reset() { ptr_.reset(); }

 public:
  // check whether the promise's callback has been invoked
  std::optional<bool> IsDone() const;

  // check whether the promise has been initialized
  std::optional<bool> IsEmpty() const;

  // check whether the result has been settled, maybe not invoke callback
  std::optional<bool> IsSettled() const;

  // check the promise has invoked |Resolve|
  std::optional<bool> IsSatisfied() const;

  // check the promise has invoked |Reject|
  std::optional<bool> IsUnsatisfied() const;

  bool IsExpired() const { return ptr_.expired(); }

  PromiseResolver() : ptr_() {}

 protected:
  explicit PromiseResolver(const std::shared_ptr<_::PromiseState<T>>& p)
      : ptr_(p) {}

 private:
  std::weak_ptr<_::PromiseState<T>> ptr_;

  template <typename U>
  friend class Promise;
};

template <typename T>
template <typename U>
bool PromiseResolver<T>::Resolve(U&& val) {
  if (auto p = ptr_.lock(); p) {
    return p->Resolve(std::forward<U>(val));
  }
  return false;
}

template <typename T>
bool PromiseResolver<T>::Reject(Error&& e) {
  if (auto p = ptr_.lock(); p) {
    return p->Reject(std::move(e));
  }
  return false;
}

template <typename T>
void PromiseResolver<T>::Cancel() {
  if (auto p = ptr_.lock(); p) {
    p->Cancel();
  }
}

template <typename T>
std::optional<bool> PromiseResolver<T>::IsDone() const {
  if (auto p = ptr_.lock(); p) {
    return p->IsDone();
  }
  return {};
}

template <typename T>
std::optional<bool> PromiseResolver<T>::IsEmpty() const {
  if (auto p = ptr_.lock(); p) {
    return p->IsEmpty();
  }
  return {};
}

template <typename T>
std::optional<bool> PromiseResolver<T>::IsSettled() const {
  if (auto p = ptr_.lock(); p) {
    return p->IsSettled();
  }
  return {};
}

template <typename T>
std::optional<bool> PromiseResolver<T>::IsUnsatisfied() const {
  if (auto p = ptr_.lock(); p) {
    return p->IsUnsatisfied();
  }
  return {};
}

template <typename T>
std::optional<bool> PromiseResolver<T>::IsSatisfied() const {
  if (auto p = ptr_.lock(); p) {
    return p->IsSatisfied();
  }
  return {};
}

template <>
class PromiseResolver<void> {
 public:
  bool Set(Result<void>&& r) {
    if (r) {
      return Resolve();
    } else {
      return Reject(r.PassError());
    }
  }

  inline bool Resolve();
  inline bool Reject(Error&&);
  inline void Cancel();

  void Reset() { ptr_.reset(); }

 public:
  // check whether the promise's callback has been invoked
  inline std::optional<bool> IsDone() const;

  // check whether the promise has been initialized
  inline std::optional<bool> IsEmpty() const;

  // check whether the result has been settled, maybe not invoke callback
  inline std::optional<bool> IsSettled() const;

  // check the promise has invoked |Resolve|
  inline std::optional<bool> IsSatisfied() const;

  // check the promise has invoked |Reject|
  inline std::optional<bool> IsUnsatisfied() const;

  bool IsExpired() const { return ptr_.expired(); }

  PromiseResolver() : ptr_() {}

 protected:
  explicit PromiseResolver(const std::shared_ptr<_::PromiseState<void>>& p)
      : ptr_(p) {}

 private:
  std::weak_ptr<_::PromiseState<void>> ptr_;

  template <typename U>
  friend class Promise;
};

inline bool PromiseResolver<void>::Resolve() {
  if (auto p = ptr_.lock(); p) {
    return p->Resolve();
  }
  return false;
}

inline bool PromiseResolver<void>::Reject(Error&& e) {
  if (auto p = ptr_.lock(); p) {
    return p->Reject(std::move(e));
  }
  return false;
}

inline void PromiseResolver<void>::Cancel() {
  if (auto p = ptr_.lock(); p) {
    p->Cancel();
  }
}

inline std::optional<bool> PromiseResolver<void>::IsDone() const {
  if (auto p = ptr_.lock(); p) {
    return p->IsDone();
  }
  return {};
}

inline std::optional<bool> PromiseResolver<void>::IsEmpty() const {
  if (auto p = ptr_.lock(); p) {
    return p->IsEmpty();
  }
  return {};
}

inline std::optional<bool> PromiseResolver<void>::IsSettled() const {
  if (auto p = ptr_.lock(); p) {
    return p->IsSettled();
  }
  return {};
}

inline std::optional<bool> PromiseResolver<void>::IsSatisfied() const {
  if (auto p = ptr_.lock(); p) {
    return p->IsSatisfied();
  }
  return {};
}

inline std::optional<bool> PromiseResolver<void>::IsUnsatisfied() const {
  if (auto p = ptr_.lock(); p) {
    return p->IsUnsatisfied();
  }
  return {};
}

template <typename T>
class Promise {
 public:
  using ValueType = T;
  using ResolverType = PromiseResolver<T>;

  Promise() : state_(std::make_shared<_::PromiseState<T>>()) {}

  explicit Promise(std::shared_ptr<_::PromiseState<T>>&& p)
      : state_(std::move(p)) {}

  Promise(Promise&&) = default;
  Promise& operator=(Promise&&) = default;

 public:
  template <typename U>
  bool Set(Result<U>&& r) {
    if (r) {
      return Resolve(r.PassResult());
    } else {
      return Reject(r.PassError());
    }
  }

  template <typename U>
  bool Resolve(U&& value) {
    return state_->Resolve(std::forward<U>(value));
  }

  bool Reject(Error&& e) { return state_->Reject(std::move(e)); }

  void Cancel() { state_->Cancel(); }

  ResolverType GetResolver() { return ResolverType{state_}; }

 public:
  bool IsDone() const { return state_->IsDone(); }
  bool IsPending() const { return state_->IsPending(); }
  bool IsSatisfied() const { return state_->IsSatisfied(); }
  bool IsUnsatisfied() const { return state_->IsUnsatisfied(); }
  bool IsSettled() const { return state_->IsSettled(); }

  bool IsEmpty() const { return state_->IsEmpty(); }
  bool IsPreFulfilled() const { return state_->IsPreFulfilled(); }
  bool IsFulfilled() const { return state_->IsFulfilled(); }
  bool IsPreRejected() const { return state_->IsPreRejected(); }
  bool IsRejected() const { return state_->IsRejected(); }
  bool IsCancelled() const { return state_->IsCancelled(); }

 public:
  bool HasHandler() const { return state_->HasHandler(); }
  Executor* GetExecutor() const { return state_->GetExecutor(); }

 public:
  template <typename F, typename RT = std::invoke_result_t<F, T>,
            typename R = typename RT::ValueType,
            std::enable_if_t<IsPromise<RT>::value, int> _ = 0>
  Promise<R> Then(F&&, Executor*);

  template <typename F, typename RT = std::invoke_result_t<F, T>,
            typename R = typename RT::ValueType,
            std::enable_if_t<IsResult<RT>::value, int> _ = 0>
  Promise<R> Then(F&&, Executor*);

  template <typename F, typename RT = std::invoke_result_t<F, T>,
            std::enable_if_t<std::is_void_v<RT>, int> _ = 0>
  void Then(F&&, Executor*);

 private:
  template <typename U>
  struct DeduceAllPromise;

  template <template <typename...> class Cntr, typename Tp>
  struct DeduceAllPromise<Result<Cntr<Promise<Tp>>>> {
    using VauleType = Cntr<Tp>;
  };

  template <typename U>
  struct DeduceAnyPromise;

  template <template <typename...> class Cntr, typename Tp>
  struct DeduceAnyPromise<Result<Cntr<Promise<Tp>>>> {
    using ValueType = Tp;
  };

  template <typename U>
  using DeduceRacePromise = DeduceAnyPromise<U>;

 public:
  // the functor |F| of following methods should return a promise container
  template <typename F, typename RT = std::invoke_result_t<F, Result<T>>,
            typename DeduceType = typename DeduceAllPromise<RT>::VauleType,
            typename R = DeduceType>
  Promise<R> ThenAll(F&& f, Executor* executor);

  template <typename F, typename RT = std::invoke_result_t<F, Result<T>>,
            typename DeduceType = typename DeduceAnyPromise<RT>::ValueType,
            typename R = DeduceType>
  Promise<R> ThenAny(F&& f, Executor* executor);

  template <typename F, typename RT = std::invoke_result_t<F, Result<T>>,
            typename DeduceType = typename DeduceRacePromise<RT>::ValueType,
            typename R = DeduceType>
  Promise<R> ThenRace(F&& f, Executor* executor);

 protected:
  std::shared_ptr<_::PromiseState<T>> state() { return state_; }
  _::PromiseState<T>* state_ptr() { return state_.get(); }

  template <typename U, typename F>
  void DoThen(_::PromiseState<U>* promise, F&& functor, Executor* executor);

  template <typename F>
  void DoThen(F&& functor, Executor* executor);

#ifdef ENABLE_CO
  void SetCoroutineHandle(std::coroutine_handle<> handle) {
    state_->SetCoroutineHandle(handle);
  }
#endif  // ENABLE_CO

 private:
  std::shared_ptr<_::PromiseState<T>> state_;

  template <typename U>
  friend class Promise;

  template <typename U>
  friend class _::PromiseState;

#ifdef ENABLE_CO
  template <typename U>
  friend class CoroutineTrait;

  template <typename U>
  friend class PromiseAwaiter;
#endif  // ENABLE_CO

  DISALLOW_COPY_AND_ASSIGN(Promise);
};

template <typename T>
template <typename U, typename F>
void Promise<T>::DoThen(_::PromiseState<U>* promise, F&& functor,
                        Executor* executor) {
  state_->Attach(promise, std::move(functor), executor);
}

template <typename T>
template <typename F>
void Promise<T>::DoThen(F&& functor, Executor* executor) {
  state_->Attach(std::move(functor), executor);
}

template <typename T>
template <typename F, typename RT, typename R,
          std::enable_if_t<IsPromise<RT>::value, int>>
Promise<R> Promise<T>::Then(F&& functor, Executor* executor) {
  Promise<R> next;
  DoThen(next.state_ptr(), std::move(functor), executor);
  return next;
}

template <typename T>
template <typename F, typename RT, typename R,
          std::enable_if_t<IsResult<RT>::value, int>>
Promise<R> Promise<T>::Then(F&& functor, Executor* executor) {
  Promise<R> next;
  DoThen(next.state_ptr(), std::move(functor), executor);
  return next;
}

template <typename T>
template <typename F, typename RT, std::enable_if_t<std::is_void_v<RT>, int>>
void Promise<T>::Then(F&& functor, Executor* executor) {
  DoThen(std::move(functor), executor);
}

template <>
class Promise<void> {
 public:
  using ValueType = void;
  using ResolverType = PromiseResolver<void>;

  Promise() : state_(std::make_shared<_::PromiseState<void>>()) {}
  explicit Promise(std::shared_ptr<_::PromiseState<void>>&& p)
      : state_(std::move(p)) {}

  Promise(Promise&&) = default;
  Promise& operator=(Promise&&) = default;

 public:
  bool Set(Result<void> r) {
    if (r) {
      return Resolve();
    } else {
      return Reject(r.PassError());
    }
  }

  bool Resolve() { return state_->Resolve(); }
  bool Reject(Error&& e) { return state_->Reject(std::move(e)); }
  void Cancel() { state_->Cancel(); }

  ResolverType GetResolver() { return ResolverType{state_}; }

  std::optional<Result<void>> PassResult() {
    if (IsSettled()) {
      return state_->PassResult();
    }
    return {};
  }

 public:
  bool IsDone() const { return state_->IsDone(); }
  bool IsPending() const { return state_->IsPending(); }
  bool IsSatisfied() const { return state_->IsSatisfied(); }
  bool IsUnsatisfied() const { return state_->IsUnsatisfied(); }
  bool IsSettled() const { return state_->IsSettled(); }

  bool IsEmpty() const { return state_->IsEmpty(); }
  bool IsPreFulfilled() const { return state_->IsPreFulfilled(); }
  bool IsFulfilled() const { return state_->IsFulfilled(); }
  bool IsPreRejected() const { return state_->IsPreRejected(); }
  bool IsRejected() const { return state_->IsRejected(); }
  bool IsCancelled() const { return state_->IsCancelled(); }

 public:
  bool HasHandler() const { return false; }
  Executor* GetExecutor() const { return nullptr; }

 protected:
  std::shared_ptr<_::PromiseState<void>> state() { return state_; }
  _::PromiseState<void>* state_ptr() { return state_.get(); }

  std::shared_ptr<_::PromiseState<void>> state_;

  template <typename U>
  friend class Promise;

  template <typename U>
  friend class _::PromiseState;

  DISALLOW_COPY_AND_ASSIGN(Promise);
};

template <typename T, typename P>
class PromiseAttachment {
 public:
  PromiseAttachment() = default;

  explicit PromiseAttachment(
      std::weak_ptr<_::PromiseStateAttachment<T, P>>&& att)
      : att_(std::move(att)) {}

  PromiseAttachment(PromiseAttachment&&) = default;
  PromiseAttachment(const PromiseAttachment&) = default;
  PromiseAttachment& operator=(PromiseAttachment&&) = default;
  PromiseAttachment& operator=(const PromiseAttachment&) = default;

  bool IsExisted() const { return !att_.expired(); }

  std::optional<T*> Get() const {
    if (auto l = att_.lock(); l) {
      return l->GetPayload();
    }
    return {};
  }

 private:
  std::weak_ptr<_::PromiseStateAttachment<T, P>> att_;
};

template <typename T, typename U = std::remove_reference_t<T>>
Promise<U> MkResolvedPromise(T&& val) {
  Promise<U> p;
  p.Resolve(std::forward<T>(val));
  return p;
}

template <typename T>
Promise<T> MkRejectedPromise(Error&& e) {
  Promise<T> p;
  p.Reject(std::move(e));
  return p;
}

template <typename T, typename F>
Promise<T> MkPromise(F&& f) {
  auto state = std::make_shared<_::PromiseState<T>>();

  auto resolver = [p = state](T&& v) mutable {
    return p->Resolve(std::forward<T>(v));
  };

  auto rejector = [p = state](Error&& e) mutable {
    return p->Reject(std::move(e));
  };

  Promise<T> p{std::move(state)};
  std::invoke(std::forward<F>(f), std::move(resolver), std::move(rejector));
  return p;
}

template <typename T, typename P, typename F, typename... ARGS>
std::pair<Promise<T>, PromiseAttachment<T, P>> MkAttachmentPromise(
    F&& f, ARGS&&... args) {
  auto state = std::make_shared<_::PromiseStateAttachment<T, P>>(
      std::forward<ARGS>(args)...);

  auto resolver = [p = state](T&& v) mutable {
    return p->Resolve(std::forward<T>(v));
  };

  auto rejector = [p = state](Error&& e) mutable {
    return p->Reject(std::move(e));
  };

  PromiseAttachment<T, P> att{state};
  Promise<T> p{std::move(state)};

  std::invoke(std::forward<F>(f), std::move(resolver), std::move(rejector));
  return std::make_pair(std::move(p), std::move(att));
}

template <typename Itr,
          typename TraitType = typename std::iterator_traits<Itr>::value_type,
          typename Type = typename TraitType::ValueType,
          typename R = std::vector<Type>>
Promise<R> MkAllPromise(Itr begin, Itr end, Executor* executor) {
  if (begin == end) {
    return MkResolvedPromise(R{});
  }

  struct Ctx {
    std::size_t success_counter;
    std::vector<Type> results;
    Ctx(std::size_t c) : success_counter(c), results(c) {}
  };

  return MkPromise<R>([begin, end, executor](auto&& resolver, auto&& rejector) {
    std::size_t idx = 0;
    auto ctx = std::make_shared<Ctx>(std::distance(begin, end));
    for (auto itr = begin; itr != end; ++itr, ++idx) {
      itr->Then(
          [ctx, resolver, rejector, idx](Result<Type>&& r) mutable {
            if (!r) {
              rejector(r.PassError());
              return;
            }

            ctx->results[idx] = r.PassResult();

            ctx->success_counter--;
            if (ctx->success_counter == 0) {
              resolver(std::move(ctx->results));
            }
          },
          executor);
    }
  });
}

template <typename Cntr>
auto MkAllPromise(Cntr&& container, Executor* executor) {
  return MkAllPromise(std::begin(container), std::end(container), executor);
}

template <typename Cntr, typename PromiseType = typename Cntr::value_type,
          typename Type = typename PromiseType::ValueType,
          typename R = std::vector<Type>>
Promise<R> MkAllPromiseAttachContainer(Cntr&& container, Executor* executor) {
  if (container.empty()) {
    return MkResolvedPromise(R{});
  }

  struct Ctx {
    std::size_t success_counter;
    std::vector<Type> results;
    Ctx(std::size_t c) : success_counter(c), results(c) {}
  };

  auto end = container.end();
  auto begin = container.begin();

  return MkAttachmentPromise<R, Cntr>(
             [begin, end, executor](auto&& resolver, auto&& rejector) {
               std::size_t idx = 0;
               auto ctx = std::make_shared<Ctx>(std::distance(begin, end));
               for (auto itr = begin; itr != end; ++itr, ++idx) {
                 itr->Then(
                     [ctx, resolver, rejector, idx](Result<Type>&& r) mutable {
                       if (!r) {
                         rejector(r.PassError());
                         return;
                       }
                       ctx->results[idx] = r.PassResult();
                       ctx->success_counter--;
                       if (ctx->success_counter == 0) {
                         resolver(std::move(ctx->results));
                       }
                     },
                     executor);
               }
             },
             std::move(container))
      .first;
}

template <typename Itr,
          typename TraitType = typename std::iterator_traits<Itr>::value_type,
          typename Type = typename TraitType::ValueType, typename R = Type>
Promise<R> MkAnyPromise(Itr begin, Itr end, Executor* executor) {
  if (begin == end) {
    return MkRejectedPromise<R>(Err(kErrorEventPromiseAny, "no promise"));
  }

  struct Ctx {
    std::size_t failure_counter;
    std::vector<Error> errors;
    Ctx(std::size_t c) : failure_counter(c), errors(c) {}
  };

  return MkPromise<R>([begin, end, executor](auto&& resolver, auto&& rejector) {
    std::size_t idx = 0;
    auto ctx = std::make_shared<Ctx>(std::distance(begin, end));
    for (auto itr = begin; itr != end; ++itr, ++idx) {
      itr->Then(
          [ctx, resolver, rejector, idx](Result<Type>&& r) mutable {
            if (r) {
              resolver(r.PassResult());
              return;
            }

            ctx->errors[idx] = r.PassError();
            ctx->failure_counter--;
            if (ctx->failure_counter == 0) {
              rejector(Err(kErrorEventPromiseAny, "no resolved promise"));
            }
          },
          executor);
    }
  });
}

template <typename Cntr>
auto MkAnyPromise(Cntr&& container, Executor* executor) {
  return MkAnyPromise(std::begin(container), std::end(container), executor);
}

template <typename Cntr, typename PromiseType = typename Cntr::value_type,
          typename Type = typename PromiseType::ValueType, typename R = Type>
Promise<R> MkAnyPromiseAttachContainer(Cntr&& container, Executor* executor) {
  if (container.empty()) {
    return MkRejectedPromise<R>(Err(kErrorEventPromiseAny, "no promise"));
  }

  struct Ctx {
    std::size_t failure_counter;
    std::vector<Error> errors;
    Ctx(std::size_t c) : failure_counter(c), errors(c) {}
  };

  auto end = container.end();
  auto begin = container.begin();

  return MkAttachmentPromise<R, Cntr>(
             [begin, end, executor](auto&& resolver, auto&& rejector) {
               std::size_t idx = 0;
               auto ctx = std::make_shared<Ctx>(std::distance(begin, end));
               for (auto itr = begin; itr != end; ++itr, ++idx) {
                 itr->Then(
                     [ctx, resolver, rejector, idx](Result<Type>&& r) mutable {
                       if (r) {
                         resolver(r.PassResult());
                         return;
                       }

                       ctx->errors[idx] = r.PassError();
                       ctx->failure_counter--;
                       if (ctx->failure_counter == 0) {
                         rejector(
                             Err(kErrorEventPromiseAny, "no resolved promise"));
                       }
                     },
                     executor);
               }
             },
             std::move(container))
      .first;
}

template <typename Itr,
          typename TraitType = typename std::iterator_traits<Itr>::value_type,
          typename ValueType = typename TraitType::ValueType,
          typename R = ValueType>
Promise<R> MkRacePromise(Itr begin, Itr end, Executor* executor) {
  if (begin == end) {
    return MkRejectedPromise<R>(Err(kErrorEventPromiseRace, "no promise"));
  }

  return MkPromise<R>([begin, end, executor](auto&& resolver, auto&& rejector) {
    for (auto itr = begin; itr != end; ++itr) {
      itr->Then(
          [resolver, rejector](Result<ValueType>&& r) mutable {
            if (r) {
              resolver(r.PassResult());
            } else {
              rejector(r.PassError());
            }
          },
          executor);
    }
  });
}

template <typename Cntr>
auto MkRacePromise(Cntr&& container, Executor* executor) {
  return MkRacePromise(std::begin(container), std::end(container), executor);
}

template <typename Cntr, typename PromiseType = typename Cntr::value_type,
          typename Type = typename PromiseType::ValueType, typename R = Type>
Promise<R> MkRacePromiseAttachContainer(Cntr&& container, Executor* executor) {
  if (container.empty()) {
    MkRejectedPromise<R>(Err(kErrorEventPromiseRace, "no promise"));
  }

  auto end = container.end();
  auto begin = container.begin();

  return MkAttachmentPromise<R, Cntr>(
             [begin, end, executor](auto&& resolver, auto&& rejector) {
               for (auto itr = begin; itr != end; ++itr) {
                 itr->Then(
                     [resolver, rejector](Result<Type>&& r) mutable {
                       if (r) {
                         resolver(r.PassResult());
                       } else {
                         rejector(r.PassError());
                       }
                     },
                     executor);
               }
             },
             std::move(container))
      .first;
}

template <typename T>
template <typename F, typename RT, typename DeduceType, typename R>
Promise<R> Promise<T>::ThenAll(F&& f, Executor* executor) {
  return Then(
      [f = std::forward<F>(f), executor](Result<T>&& r) mutable {
        auto result = std::invoke(std::forward<F>(f), std::move(r));
        if (result) {
          return MkAllPromiseAttachContainer(result.PassResult(), executor);
        } else {
          return MkRejectedPromise<R>(result.PassError());
        }
      },
      executor);
}

template <typename T>
template <typename F, typename RT, typename DeduceType, typename R>
Promise<R> Promise<T>::ThenAny(F&& f, Executor* executor) {
  return Then(
      [f = std::forward<F>(f), executor](Result<T>&& r) mutable {
        auto result = std::invoke(std::forward<decltype(f)>(f), std::move(r));
        if (result) {
          return MkAnyPromiseAttachContainer(result.PassResult(), executor);
        } else {
          return MkRejectedPromise<R>(result.PassError());
        }
      },
      executor);
}

template <typename T>
template <typename F, typename RT, typename DeduceType, typename R>
Promise<R> Promise<T>::ThenRace(F&& f, Executor* executor) {
  return Then(
      [f = std::forward<F>(f), executor](Result<T>&& r) mutable {
        auto result = std::invoke(std::forward<decltype(f)>(f), std::move(r));
        if (result) {
          return MkRacePromiseAttachContainer(result.PassResult(), executor);
        } else {
          return MkRejectedPromise<R>(result.PassError());
        }
      },
      executor);
}

template <typename T>
void _::PromiseState<T>::PropagatePromise(void* promise) {
  auto* inner_promise = reinterpret_cast<Promise<T>*>(promise);
  auto inner_state = inner_promise->state();
  DCHECK(!inner_state->HasHandler());

  Watch(inner_state.get());

  inner_promise->DoThen(
      [](Result<T>&& r) mutable -> Result<T> { return std::move(r); }, nullptr);
}

inline void _::PromiseState<void>::PropagatePromise(void* promise) {
  auto* inner_promise = reinterpret_cast<Promise<void>*>(promise);
  auto inner_state = inner_promise->state();

  inner_state->set_next(this);
}

// promise specialization mainly used to do notification
class NotifierResolver final : public PromiseResolver<libz::Dummy> {
 public:
  bool Resolve() {
    return PromiseResolver<libz::Dummy>::Resolve(libz::Dummy{});
  }

 private:
  explicit NotifierResolver(
      const std::shared_ptr<_::PromiseState<libz::Dummy>>& p)
      : PromiseResolver<libz::Dummy>(p) {}

  friend class Notifier;
};

class Notifier final : public Promise<libz::Dummy> {
 public:
  using ResolverType = NotifierResolver;

  Notifier() = default;
  explicit Notifier(std::shared_ptr<_::PromiseState<libz::Dummy>>&& p)
      : Promise<libz::Dummy>(std::move(p)) {}

  Notifier(Notifier&&) = default;
  Notifier& operator=(Notifier&&) = default;

  ResolverType GetResolver() { return ResolverType(state()); }

 public:
  // since event notifier is actually just a unary promise chain, we should not
  // invoke |watch| operation.
  template <typename F, typename RT = std::invoke_result_t<F, Error>>
  void Then(F&& f, Executor* executor) {
    static_assert(std::is_same_v<RT, void>,
                  "callback of |Then| must be void(Error&&)");
    auto cb =
        [f = std::forward<F>(f)](Result<libz::Dummy>&& r) mutable -> void {
      if (r) {
        std::invoke(std::forward<F>(f), Error{});
      } else {
        std::invoke(std::forward<F>(f), r.PassError());
      }
    };

    Promise<libz::Dummy>::Then(std::move(cb), executor);
  }
};

inline Notifier MkResolvedNotifier() {
  Notifier nfr;
  nfr.GetResolver().Resolve();
  return nfr;
}

inline Notifier MkRejectedNotifier(Error&& e) {
  Notifier nfr;
  nfr.GetResolver().Reject(std::move(e));
  return nfr;
}

}  // namespace event
}  // namespace libz
