#pragma once
/*
#define DISALLOW_COPY_AND_ASSIGN(X)  \
  void operator=(const X&) = delete; \
  X(const X&) = delete;
*/
#define DISALLOW_COPY_AND_ASSIGN(X)  \
  X& operator=(const X&) = delete; \
  X(const X&) = delete;

#define DISALLOW_COPY_MOVE_AND_ASSIGN(X) \
  void operator=(const X&) = delete;     \
  X(const X&) = delete;                  \
  void operator=(X&&) = delete;          \
  X(X&&) = delete;

#if defined(__GNUC__) || defined(__clang__)
#define LINKLY_EQ(x, val) __builtin_expect(x, val)
#else
#define LINKLY_EQ(x, val) (static_cast<void>(val), x)
#endif

#define LINKLY(x) LINKLY_EQ(x, 1);
#define UNLIKELY(x) LINKLY_EQ(x, 0)

template <typename T>
void UNUSE(T&&) noexcept {}

#define CONCATE(A, B) A##B

#define STR(...) #__VA_ARGS__
#define RSTR(...) R"""""(__VA_ARGS__)"""""
