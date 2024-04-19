#include "basic.h"

#include <base/error.h>
#include <fmt/format.h>

namespace libz {
namespace event {
namespace {

struct EventCategory : public Error::Category {
  const char* GetName() const override { return "event"; }
  std::string GetInformation(int c) const override {
    switch (c) {
#define __(A, B) \
  case A:        \
    return fmt::format("event[{}]", B);
      EVENT_ERROR_LIST(__)
#undef __
      default:
        return "event[none]";
    }
  }
};

}  // namespace

const Error::Category* Cat() {
  static EventCategory kC;
  return &kC;
}

}  // namespace event
}  // namespace libz
