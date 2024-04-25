#include "error.h"

#include <unordered_map>

namespace libz {

namespace {
class BoostErrorCategory : public Error::Category {
 public:
  const char* GetName() const override { return "boost"; }
  std::string GetInformation(int code) const override {
    return fmt::format("boost[error] ec: {}", code);
  }
};

class SyscallErrorCategory : public Error::Category {
 public:
  const char* GetName() const override { return "syscall"; }
  std::string GetInformation(int code) const override {
    return fmt::format("syscall[error] errno: {}", code);
  }
};

class GeneralErrorCategory : public Error::Category {
 public:
  GeneralErrorCategory() = default;
  GeneralErrorCategory(std::string_view category) : category_(category) {}

  const char* GetName() const override { return category_.data(); }
  std::string GetInformation(int code) const override {
    return fmt::format("{}[error] ec: {}", category_, code);
  }

 private:
  std::string category_;
};

}  // namespace

BoostErrorCategory* GetBoostErrorCategory() {
  static BoostErrorCategory kC;
  return &kC;
}

GeneralErrorCategory* GetGeneralErrorCategory(std::string category) {
  static std::unordered_map<std::string, GeneralErrorCategory> kC;
  if (auto itr = kC.find(category); itr == kC.end()) {
    kC[category] = GeneralErrorCategory(category);
  }
  return &kC[category];
}

SyscallErrorCategory* GetSyscallErrorCategory() {
  static SyscallErrorCategory kC;
  return &kC;
}

bool Error::IsSysError() const {
  return category() == GetSyscallErrorCategory();
}

bool Error::IsBoostError() const {
  return category() == GetBoostErrorCategory();
}

Error Error::MkSysError(int sys_errno) {
  return Error{GetSyscallErrorCategory(), sys_errno};
}

Error Error::MkBoostError(int code, std::string&& msg) {
  if (code == 0) return {};
  return Error{GetBoostErrorCategory(), code, std::move(msg)};
}

Error Error::MkBoostError(int code, const std::string& msg) {
  if (code == 0) return {};
  return Error{GetBoostErrorCategory(), code, msg};
}

Error Error::MkGeneralError(int code, std::string&& msg,
                            std::string_view category) {
  return Error(GetGeneralErrorCategory(std::string(category)), code,
               std::move(msg));
}

Error Error::MkGeneralError(int code, const std::string& msg,
                            std::string_view category) {
  return Error(GetGeneralErrorCategory(std::string(category)), code, msg);
}

}  // namespace libz
