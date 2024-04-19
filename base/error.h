#pragma once

#include <fmt/format.h>

#include <optional>
#include <string>

#include "check.h"

namespace libz {

class Error {
 public:
  class Category {
   public:
    virtual const char* GetName() const = 0;
    virtual std::string GetInformation(int code) const = 0;

    virtual ~Category() {}
  };

  static constexpr std::uint32_t kNoErrorCode = 0;

 public:
  Error() : code_(kNoErrorCode), category_(nullptr), message_() {}
  Error(const Category* category, int code)
      : code_(code), category_(category), message_() {}
  Error(const Category* category, int code, const std::string& msg)
      : code_(code), category_(category), message_(msg) {}
  Error(const Category* category, int code, std::string&& msg)
      : code_(code), category_(category), message_(std::move(msg)) {}

  Error(Error&&) = default;
  Error(const Error&) = default;
  Error& operator=(Error&&) = default;
  Error& operator=(const Error&) = default;

 public:
  static Error MkSysError(int sys_errno);
  static Error MkBoostError(int code, std::string&& msg);
  static Error MkBoostError(int code, const std::string& msg);

  bool IsSysError() const;
  bool IsBoostError() const;

 public:
  int code() const { return code_; }
  const Category* category() const { return category_; }
  std::string information() const { return category_->GetInformation(code_); }

  bool Has() const { return category_ != nullptr; }
  operator bool() const { return Has(); }

  bool HasMessage() const { return message_.has_value(); }

  inline std::string PassMessage();
  inline std::string& GetMessage();
  inline const std::string& GetMessage() const;
  inline std::string Details() const;

  inline void Clear();

 private:
  int code_;
  const Category* category_;
  std::optional<std::string> message_;
};

inline void Error::Clear() {
  category_ = nullptr;
  message_.reset();
  code_ = kNoErrorCode;
}

inline const std::string& Error::GetMessage() const {
  DCHECK(HasMessage());
  return message_.value();
}

inline std::string& Error::GetMessage() {
  DCHECK(HasMessage());
  return message_.value();
}

inline std::string Error::PassMessage() {
  DCHECK(HasMessage());
  auto result = std::move(*message_);
  message_.reset();
  return result;
}

inline std::string Error::Details() const {
  return fmt::format("{}: {}", information(),
                     message_ ? *message_ : std::string{});
}

}  // namespace libz
