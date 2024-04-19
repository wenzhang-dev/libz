#pragma once

#include <base/error.h>
#include <fmt/format.h>

#define EVENT_ERROR_LIST(__)                                  \
  __(kErrorEventPromiseAny, "promise any operation failed")   \
  __(kErrorEventPromiseRace, "promise race operation failed") \
  __(kErrorEventLoopShutdown, "eventloop shutdown")           \
  __(kErrorUnsupportedEvent, "event unsupported")             \
  __(kErrorCoroutineException, "coroutine exception")

namespace libz {
namespace event {

enum EventError {
#define __(A, B) A,
  EVENT_ERROR_LIST(__)
#undef __
};

const Error::Category* Cat();

inline Error Err(EventError e) { return Error{Cat(), e}; }

template <typename... Args>
Error Err(EventError e, const Args&... args) {
  auto msg = fmt::format(args...);
  return Error{Cat(), e, std::move(msg)};
}

enum class Severity {
  kUrgent,
  kCritical,
  kNormal,
};

}  // namespace event
}  // namespace libz
