#pragma once

#include <functional>
#include <type_traits>

namespace libz {
namespace _ {

template <typename T, typename SFINAE = void>
struct IsFunctorObject : std::false_type {};

template <typename T>
struct IsFunctorObject<T, std::void_t<decltype(&T::operator())>>
    : std::true_type {};

// extrator for functor/functor-like/member-function signature
template <typename T, typename Sig = decltype(&(T::operator()))>
struct ExtractFunctorSig {};

// partial specialization for member function, non-const pointer version
template <typename T, typename R, typename... ARGS>
struct ExtractFunctorSig<T, R (T::*)(ARGS...)> {
  using ReturnType = R;
  using Signature = R(ARGS...);
  using WrapperSignature = Signature;
  using WrapperType = std::function<WrapperSignature>;
};

// partial specialization for member function, const pointer version
template <typename T, typename R, typename... ARGS>
struct ExtractFunctorSig<T, R (T::*)(ARGS...) const> {
  using ReturnType = R;
  using Signature = R(ARGS...) const;
  using WrapperSignature = R(ARGS...);
  using WrapperType = std::function<WrapperSignature>;
};

// concrete implementation of extract function signature trait
template <typename SIG, bool Callable>
struct ExtractFunctionSigInternal : std::false_type {};

// global/free functions for pointer/reference version
template <typename R, typename... ARGS>
struct ExtractFunctionSigInternal<R (&)(ARGS...), false> : std::true_type {
  using ReturnType = R;
  using Signature = R(ARGS...);
  using WrapperSignature = Signature;
  using WrapperType = std::function<WrapperSignature>;
};

template <typename R, typename... ARGS>
struct ExtractFunctionSigInternal<R (*)(ARGS...), false> : std::true_type {
  using ReturnType = R;
  using Signature = R(ARGS...);
  using WrapperSignature = Signature;
  using WrapperType = std::function<WrapperSignature>;
};

// member functor traits
template <typename T>
struct ExtractFunctionSigInternal<T, true> : std::true_type {
  using ReturnType = typename ExtractFunctorSig<T>::ReturnType;
  using Signature = typename ExtractFunctorSig<T>::Signature;
  using WrapperSignature = typename ExtractFunctorSig<T>::WrapperSignature;
  using WrapperType = typename ExtractFunctorSig<T>::WrapperType;
};

}  // namespace _

template <typename SIG>
struct ExtractFunctionSig {
  static_assert(!std::is_member_function_pointer<SIG>::value,
                "cannot work with member function");

  using ImplType =
      _::ExtractFunctionSigInternal<SIG, _::IsFunctorObject<SIG>::value>;

  using ReturnType = typename ImplType::ReturnType;
  using Signature = typename ImplType::Signature;
  using WrapperSignature = typename ImplType::WrapperSignature;
  using WrapperType = typename ImplType::WrapperType;

  static constexpr bool kValue = ImplType::value;
};

}  // namespace libz
