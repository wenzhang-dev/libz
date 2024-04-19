#pragma once

#include <cassert>
#include <cstdlib>

#include "macros.h"

#if defined(NDEBUG)

#define DCHECK(condition)                                        \
  do {                                                           \
    false ? static_cast<void>(condition) : static_cast<void>(0); \
  } while (false)

#else  // !defined(NDEBUG)

#define DCHECK(condition) assert(condition)

#endif  // defined(NDEBUG)

#define UNREACHABLE() DCHECK(false)

#define CHECK(condition)                      \
  do {                                        \
    if (UNLIKELY(!(condition))) std::abort(); \
  } while (false)
