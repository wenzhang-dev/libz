#pragma once

#include <iostream>
#include <memory>

#include "trait.h"

namespace libz {
namespace _ {
struct WrapperFunctor {};

// StrongFunctor with strong ownership for object
template <typename Functor, typename Object>
struct StrongFunctor : WrapperFunctor {
  using ReturnType = typename ExtractFunctionSig<Functor>::ReturnType;

  StrongFunctor(std::shared_ptr<Object>&& ptr, Functor&& f)
      : strong_ptr(std::move(ptr)), functor(std::move(f)) {}

  template <typename... ARGS>
  ReturnType operator()(ARGS&&... args) {
    return std::forward<Functor>(functor)(std::forward<ARGS>(args)...);
  }

  bool IsSilent() const { return false; }
  operator bool() const { return !IsSilent(); }

 public:
  std::shared_ptr<Object> strong_ptr;
  Functor functor;
};

// WeakFunctor, maybe functor is slient
template <typename Functor, typename Object>
struct WeakFunctorBase : WrapperFunctor {
  using ReturnType = typename ExtractFunctionSig<Functor>::ReturnType;

  WeakFunctorBase(std::weak_ptr<Object>&& ptr, Functor&& f)
      : weak_ptr(std::move(ptr)), functor(std::move(f)) {}

  bool IsSilent() const { return weak_ptr.expired(); }
  operator bool() const { return !IsSilent(); }

 public:
  std::weak_ptr<Object> weak_ptr;
  Functor functor;
};

template <typename Functor, typename Object>
struct WeakFunctorNonVoidReturn : WeakFunctorBase<Functor, Object> {
  using Base = WeakFunctorBase<Functor, Object>;
  using ReturnType = typename Base::ReturnType;

  WeakFunctorNonVoidReturn(std::weak_ptr<Object>&& ptr, Functor&& f)
      : Base(std::move(ptr), std::move(f)) {}

  template <typename... ARGS>
  ReturnType operator()(ARGS&&... args) {
    auto strong_ptr{Base::weak_ptr.lock()};
    if (!strong_ptr) {
      return ReturnType{};
    }
    return std::forward<Functor>(Base::functor)(std::forward<ARGS>(args)...);
  }
};

template <typename Functor, typename Object>
struct WeakFunctorVoidReturn : WeakFunctorBase<Functor, Object> {
  using Base = WeakFunctorBase<Functor, Object>;
  using ReturnType = typename Base::ReturnType;

  static_assert(std::is_void<ReturnType>::value);

  WeakFunctorVoidReturn(std::weak_ptr<Object>&& ptr, Functor&& f)
      : Base(std::move(ptr), std::move(f)) {}

  template <typename... ARGS>
  void operator()(ARGS&&... args) {
    auto strong_ptr{Base::weak_ptr.lock()};
    if (strong_ptr) {
      std::forward<Functor>(Base::functor)(std::forward<ARGS>(args)...);
    }
  }
};

}  // namespace _

template <typename T>
struct IsWrapperFunctor {
  static constexpr bool kValue = std::is_base_of<_::WrapperFunctor, T>::value;
};

// BindWeak
template <typename Object, typename F,
          typename _R0 = _::WeakFunctorVoidReturn<F, Object>,
          typename _R1 = _::WeakFunctorNonVoidReturn<F, Object>,
          typename WeakObject = typename std::conditional<
              std::is_void<typename ExtractFunctionSig<F>::ReturnType>::value,
              _R0, _R1>::type>
WeakObject BindWeakFunctor(std::weak_ptr<Object>&& weak_ptr, F&& callback) {
  return WeakObject{std::move(weak_ptr), std::forward<F>(callback)};
}

template <typename Object, typename F>
typename ExtractFunctionSig<F>::WrapperType BindWeak(
    std::weak_ptr<Object>&& weak_ptr, F&& callback) {
  using WrapperType = typename ExtractFunctionSig<F>::WrapperType;
  return WrapperType{
      BindWeakFunctor(std::move(weak_ptr), std::forward<F>(callback))};
}

template <typename Object, typename F>
typename ExtractFunctionSig<F>::WrapperType BindWeak(Object* raw,
                                                     F&& callback) {
  return BindWeak(raw->weak_from_this(), std::forward<F>(callback));
}

// BindStrong
template <typename Object, typename F>
_::StrongFunctor<F, Object> BindStrongFunctor(std::shared_ptr<Object>&& ptr,
                                              F&& callback) {
  using StrongObject = _::StrongFunctor<F, Object>;
  return StrongObject{std::move(ptr), std::forward<F>(callback)};
}

template <typename Object, typename F>
typename ExtractFunctionSig<F>::WrapperType BindStrong(
    std::shared_ptr<Object>&& ptr, F&& callback) {
  using WrapperType = typename ExtractFunctionSig<F>::WrapperType;
  return WrapperType{
      BindStrongFunctor(std::move(ptr), std::forward<F>(callback))};
}

template <typename Object, typename F>
typename ExtractFunctionSig<F>::WrapperType BindStrong(Object* raw,
                                                       F&& callback) {
  return BindStrong(raw->shared_from_this(), std::forward<F>(callback));
}

}  // namespace libz
