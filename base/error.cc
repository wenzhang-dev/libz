#include "error.h"

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

}  // namespace

BoostErrorCategory* GetBoostErrorCategory() {
  static BoostErrorCategory kC;
  return &kC;
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

}  // namespace libz
