#pragma once

#include <optional>
#include <variant>

#include "check.h"
#include "error.h"

namespace libz {

template <typename T>
class Result {
 public:
  using ValueType = T;

  Result() : v_() {}
  Result(const T& ok) : v_(ok) {}
  Result(T&& ok) : v_(std::move(ok)) {}

  Result(const Error& err) : v_(err) {}
  Result(Error&& err) : v_(std::move(err)) {}

  Result(Result&&) = default;
  Result(const Result&) = default;

  Result& operator=(Result&&) = default;
  Result& operator=(const Result&) = default;

 public:
  bool IsEmpty() const { return v_.index() == kEmpty; }
  bool IsOk() const { return v_.index() == kOk; }
  bool IsError() const { return v_.index() == kError; }
  operator bool() const { return IsOk(); }

  void Clear() { v_ = Null{}; }

 public:
  T PassResult();
  const T& GetResult() const;

  Error PassError();
  const Error& GetError() const;

 private:
  struct Null {};
  enum { kEmpty, kOk, kError };
  std::variant<Null, T, Error> v_;
};

template <>
class Result<void> {
 public:
  using ValueType = void;

  Result() : v_() {}
  Result(const Error& err) : v_(err) {}
  Result(Error&& err) : v_(std::move(err)) {}

  Result(Result&&) = default;
  Result(const Result&) = default;

  Result& operator=(Result&&) = default;
  Result& operator=(const Result&) = default;

 public:
  bool IsEmpty() const { return false; }
  bool IsOk() const { return static_cast<bool>(!v_); }
  bool IsError() const { return !IsOk(); }
  operator bool() const { return IsOk(); }

  void Clear() { v_.Clear(); }

 public:
  Error PassError() { return std::move(v_); }
  const Error& GetError() const { return v_; }

 private:
  Error v_;
};

template <typename T>
struct IsResult : std::false_type {};

template <typename T>
struct IsResult<Result<T>> : std::true_type {};

template <typename T>
T Result<T>::PassResult() {
  auto ptr = std::get_if<T>(&v_);

  DCHECK(ptr);
  auto result = std::move(*ptr);

  Clear();
  return result;
}

template <typename T>
const T& Result<T>::GetResult() const {
  auto ptr = std::get_if<T>(&v_);

  DCHECK(ptr);
  return *ptr;
}

template <typename T>
Error Result<T>::PassError() {
  auto ptr = std::get_if<Error>(&v_);

  DCHECK(ptr);
  auto error = std::move(*ptr);

  Clear();
  return error;
}

template <typename T>
const Error& Result<T>::GetError() const {
  auto ptr = std::get_if<Error>(&v_);

  DCHECK(ptr);
  return *ptr;
}

}  // namespace libz
