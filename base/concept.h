#pragma once

// the usage is as following, the |_| equals a alias to the referenced class
// DEFINE_CONCEPT(MyConcept, bool (_::*)() const, &_::IsAbort);

#define DEFINE_CONCEPT(NAME, SIG, METHOD)                                   \
  template <typename __T>                                                   \
  struct NAME {                                                             \
   private:                                                                 \
    template <typename _, SIG>                                              \
    struct SFINAE {};                                                       \
    template <typename _>                                                   \
    static char Check(SFINAE<_, METHOD>*);                                  \
    template <typename _>                                                   \
    static int Check(...);                                                  \
                                                                            \
   public:                                                                  \
    static constexpr bool kValue = (sizeof(Check<__T>(0)) == sizeof(char)); \
  };

