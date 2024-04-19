#define CATCH_CONFIG_PREFIX_ALL
#include "promise.h"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <list>
#include <string>

#include "executor.h"

namespace libz {
namespace event {

struct MockExecutor : public Executor {
  using Callback = std::function<void()>;
  std::size_t count{0};
  std::list<Callback> queue;

  void Post(Callback&& f) { queue.push_back(std::move(f)); }

  void Run() {
    while (!queue.empty()) {
      auto cb = std::move(queue.front());
      queue.pop_front();
      cb();
      ++count;
    }
  }

  bool RunOne() {
    if (!queue.empty()) {
      auto cb = std::move(queue.front());
      queue.pop_front();
      cb();
      ++count;
      return true;
    }
    return false;
  }
};

class PromiseErrorCategory : public Error::Category {
 public:
  const char* GetName() const override { return "promise"; }

  std::string GetInformation(int code) const override {
    return "promise[null]";
  }
};

const Error::Category* Cat() {
  static PromiseErrorCategory kC;
  return &kC;
}

template <typename T>
Promise<T> MkP(typename Promise<T>::ResolverType* r) {
  Promise<T> tmp;
  *r = tmp.GetResolver();
  return tmp;
}

CATCH_TEST_CASE("basic path1", "[promise]") {
  {
    MockExecutor exec;

    int value = 0;
    Promise<int> p1;

    CATCH_REQUIRE(p1.IsEmpty());

    Promise<void> p2 = p1.Then(
        [&](Result<int>&& v) -> Result<void> {
          value = v.GetResult();
          return {};
        },
        &exec);

    CATCH_REQUIRE(p1.IsEmpty());
    CATCH_REQUIRE(p2.IsEmpty());

    CATCH_REQUIRE(exec.queue.size() == 0);

    CATCH_REQUIRE(p1.GetResolver().Resolve(2022));

    CATCH_REQUIRE(exec.queue.size() == 1);
    CATCH_REQUIRE(p1.IsPending());
    CATCH_REQUIRE(p2.IsEmpty());

    exec.Run();

    CATCH_REQUIRE(value == 2022);
    CATCH_REQUIRE(p1.IsFulfilled());
    CATCH_REQUIRE(p2.IsFulfilled());
  }

  {
    MockExecutor exec;

    int value = 0;
    Promise<int> p1;
    CATCH_REQUIRE(p1.IsEmpty());

    auto p2 = p1.Then(
        [&](Result<int>&& r) -> Result<void> {
          value = 2023;
          return Error(Cat(), 0, "promise");
        },
        &exec);

    CATCH_REQUIRE(p1.IsEmpty());
    CATCH_REQUIRE(p2.IsEmpty());

    CATCH_REQUIRE(exec.queue.size() == 0);

    CATCH_REQUIRE(p1.GetResolver().Resolve(2022));

    CATCH_REQUIRE(exec.queue.size() == 1);
    CATCH_REQUIRE(p1.IsPending());
    CATCH_REQUIRE(p2.IsEmpty());

    exec.Run();

    CATCH_REQUIRE(value == 2023);
    CATCH_REQUIRE(p1.IsFulfilled());
    CATCH_REQUIRE(p2.IsRejected());

    auto opt_result = p2.PassResult();
    CATCH_REQUIRE(opt_result);
    CATCH_REQUIRE(!opt_result.value());
    CATCH_REQUIRE(opt_result->GetError().GetMessage() == "promise");
  }

  {
    MockExecutor exec;

    int value = 0;

    std::optional<Promise<bool>> wrapper_promise;
    std::optional<Promise<bool>::ResolverType> resolver;

    {
      Promise<int> p1;
      CATCH_REQUIRE(p1.IsEmpty());
      auto p2 = p1.Then(
          [&](Result<int>&& r) -> Promise<bool> {
            if (!r) {
              return MkRejectedPromise<bool>(r.PassError());
            } else {
              value = r.GetResult();
              Promise<bool> p;
              resolver.emplace(p.GetResolver());
              return p;
            }
          },
          &exec);

      wrapper_promise.emplace(std::move(p2));
      p1.Resolve(1024);
    }

    CATCH_REQUIRE(!resolver);
    CATCH_REQUIRE(exec.count == 0);

    exec.RunOne();

    CATCH_REQUIRE(exec.count == 1);
    CATCH_REQUIRE(value == 1024);
    CATCH_REQUIRE(resolver);

    resolver->Resolve(true);
    //   wrapper_promise->Resolve(true);
    CATCH_REQUIRE(wrapper_promise);
    CATCH_REQUIRE(wrapper_promise->IsPreFulfilled());

    bool b = false;
    wrapper_promise->Then([&](Result<bool>&& r) { b = r.GetResult(); }, &exec);

    CATCH_REQUIRE(!b);

    exec.RunOne();

    CATCH_REQUIRE(wrapper_promise->IsFulfilled());
    CATCH_REQUIRE(b);
  }

  {
    MockExecutor exec;

    int v1 = 0;
    std::string v2;

    Promise<int> p1;
    CATCH_REQUIRE(p1.IsEmpty());

    auto p2 = p1.Then(
        [&](Result<int>&& r) -> Result<std::string> {
          v1 = r.GetResult();
          return std::string{"hi"};
        },
        &exec);

    auto p3 = p2.Then(
        [&](Result<std::string>&& r) -> Result<void> {
          v2 = r.PassResult();
          return {};
        },
        &exec);

    CATCH_REQUIRE(p1.IsEmpty());
    CATCH_REQUIRE(p2.IsEmpty());
    CATCH_REQUIRE(p3.IsEmpty());

    CATCH_REQUIRE(p1.GetResolver().Resolve(123));

    CATCH_REQUIRE(p1.IsPending());
    CATCH_REQUIRE(p2.IsEmpty());
    CATCH_REQUIRE(p3.IsEmpty());

    CATCH_REQUIRE(exec.count == 0);
    exec.Run();
    CATCH_REQUIRE(exec.count == 2);

    CATCH_REQUIRE(p1.IsFulfilled());
    CATCH_REQUIRE(p2.IsFulfilled());
    CATCH_REQUIRE(p3.IsFulfilled());

    CATCH_REQUIRE(v1 == 123);
    CATCH_REQUIRE(v2 == "hi");
  }

  {
    MockExecutor exec;

    int v1 = 0;
    std::string v2;

    Promise<int>::ResolverType r;

    auto p = MkP<int>(&r)
                 .Then(
                     [&](Result<int>&& r) -> Result<std::string> {
                       v1 = r.GetResult();
                       return std::string{"abc"};
                     },
                     &exec)
                 .Then(
                     [&](Result<std::string>&& r) -> Result<void> {
                       v2 = r.PassResult();
                       return {};
                     },
                     &exec);

    CATCH_REQUIRE(p.IsEmpty());
    CATCH_REQUIRE(r.Resolve(111));
    CATCH_REQUIRE(r.IsSettled());
    CATCH_REQUIRE(p.IsEmpty());

    exec.Run();

    CATCH_REQUIRE(p.IsFulfilled());

    CATCH_REQUIRE(v1 == 111);
    CATCH_REQUIRE(v2 == "abc");

    auto opt_result = p.PassResult();
    CATCH_REQUIRE(opt_result);
  }

  {
    MockExecutor exec;

    auto p = MkResolvedPromise(2022);
    CATCH_REQUIRE(p.IsPending());
    CATCH_REQUIRE(p.IsPreFulfilled());

    int value = 0;
    p.Then(
        [&](Result<int>&& r) -> Result<void> {
          value = r.GetResult();
          return {};
        },
        &exec);

    Result(p.IsPreFulfilled());

    exec.Run();

    CATCH_REQUIRE(p.IsFulfilled());
    CATCH_REQUIRE(value == 2022);
  }

  {
    MockExecutor exec;

    auto p1 = MkRejectedPromise<int>(Error(Cat(), 0, "rejected"));
    CATCH_REQUIRE(p1.IsPending());
    CATCH_REQUIRE(p1.IsPreRejected());

    auto p2 = p1.Then(
        [&](Result<int>&& r) -> Result<void> { return r.PassError(); }, &exec);

    CATCH_REQUIRE(p1.IsPreRejected());
    CATCH_REQUIRE(p2.IsEmpty());

    exec.Run();

    CATCH_REQUIRE(p1.IsRejected());
    CATCH_REQUIRE(p2.IsRejected());

    auto opt_result = p2.PassResult();
    CATCH_REQUIRE(opt_result);
    CATCH_REQUIRE(!opt_result.value());
    CATCH_REQUIRE(opt_result->GetError().GetMessage() == "rejected");
  }

  {
    MockExecutor exec;

    std::string v;
    Result<std::string> r{"123"};

    // In fact, MkPromise returns a rejected or resolved promise
    auto p = MkPromise<std::string>([&](auto&& resolver, auto&& rejector) {
      if (r) {
        resolver(r.PassResult());
      } else {
        rejector(r.PassError());
      }
    });

    CATCH_REQUIRE(p.IsPending());
    CATCH_REQUIRE(p.IsPreFulfilled());

    p.Then(
        [&](Result<std::string>&& r) -> Result<void> {
          v = r.PassResult();
          return {};
        },
        &exec);

    CATCH_REQUIRE(p.IsPreFulfilled());

    exec.Run();

    CATCH_REQUIRE(p.IsFulfilled());
    CATCH_REQUIRE(v == "123");
  }

  {
    MockExecutor exec;

    std::vector<Promise<int>> promise_list;
    promise_list.push_back(MkResolvedPromise(1));
    promise_list.push_back(MkResolvedPromise(2));
    promise_list.push_back(MkResolvedPromise(3));

    std::vector<int> rets;

    auto p = MkAllPromise(promise_list.begin(), promise_list.end(), &exec);
    p.Then(
        [&](Result<std::vector<int>>&& r) -> Result<void> {
          rets = r.PassResult();
          return {};
        },
        &exec);

    CATCH_REQUIRE(p.IsEmpty());

    // all callback of promise_list will be invoked
    // it causes the |p| promise to be resolved
    exec.Run();

    CATCH_REQUIRE(p.IsFulfilled());
    CATCH_REQUIRE(rets[0] == 1);
    CATCH_REQUIRE(rets[1] == 2);
    CATCH_REQUIRE(rets[2] == 3);
  }

  {
    MockExecutor exec;
    std::vector<Promise<int>> promise_list;
    promise_list.push_back(MkResolvedPromise(4));
    promise_list.push_back(MkResolvedPromise(5));
    promise_list.push_back(MkResolvedPromise(6));

    std::vector<int> rets;

    auto p = MkAllPromise(promise_list, &exec);
    p.Then(
        [&](Result<std::vector<int>>&& r) -> Result<void> {
          rets = r.PassResult();
          return {};
        },
        &exec);

    exec.Run();

    CATCH_REQUIRE(p.IsFulfilled());
    CATCH_REQUIRE(rets[0] == 4);
    CATCH_REQUIRE(rets[1] == 5);
    CATCH_REQUIRE(rets[2] == 6);
  }

  {
    MockExecutor exec;

    std::vector<Promise<bool>> promise_list;
    promise_list.push_back(MkResolvedPromise(true));
    promise_list.push_back(MkResolvedPromise(false));
    promise_list.push_back(MkRejectedPromise<bool>(Error(Cat(), 0, "err")));

    auto p1 = MkAllPromise(promise_list, &exec);
    auto p2 = p1.Then(
        [&](Result<std::vector<bool>>&& r) -> Result<void> {
          return r.PassError();
        },
        &exec);

    CATCH_REQUIRE(p1.IsEmpty());
    CATCH_REQUIRE(p2.IsEmpty());

    exec.Run();

    CATCH_REQUIRE(p1.IsRejected());
    CATCH_REQUIRE(p2.IsRejected());

    auto opt_result = p2.PassResult();
    CATCH_REQUIRE(opt_result);
    CATCH_REQUIRE(!opt_result.value());
    CATCH_REQUIRE(opt_result->GetError().GetMessage() == "err");
  }

  {
    MockExecutor exec;

    std::vector<Promise<int>> promise_list;
    promise_list.push_back(MkRejectedPromise<int>(Error(Cat(), 0, "err")));
    promise_list.push_back(MkRejectedPromise<int>(Error(Cat(), 1, "err")));
    promise_list.push_back(MkResolvedPromise(123));

    int value = 0;

    auto p = MkAnyPromise(promise_list.begin(), promise_list.end(), &exec);
    p.Then(
        [&](Result<int>&& r) -> Result<void> {
          value = r.PassResult();
          return {};
        },
        &exec);

    CATCH_REQUIRE(p.IsEmpty());

    exec.Run();

    CATCH_REQUIRE(p.IsFulfilled());
    CATCH_REQUIRE(value == 123);
  }

  {
    MockExecutor exec;

    std::vector<Promise<int>> promise_list;
    promise_list.push_back(MkRejectedPromise<int>(Error(Cat(), 0, "err")));
    promise_list.push_back(MkRejectedPromise<int>(Error(Cat(), 1, "err")));
    promise_list.push_back(MkRejectedPromise<int>(Error(Cat(), 2, "err")));

    auto p1 = MkAnyPromise(promise_list.begin(), promise_list.end(), &exec);
    auto p2 = p1.Then(
        [&](Result<int>&& r) -> Result<void> { return r.PassError(); }, &exec);

    CATCH_REQUIRE(p1.IsEmpty());
    CATCH_REQUIRE(p2.IsEmpty());

    exec.Run();

    CATCH_REQUIRE(p1.IsRejected());
    CATCH_REQUIRE(p2.IsRejected());

    auto opt_result = p2.PassResult();
    CATCH_REQUIRE(opt_result);
    CATCH_REQUIRE(!opt_result.value());
    CATCH_REQUIRE(opt_result->GetError().GetMessage() == "no resolved promise");
  }

  {
    MockExecutor exec;

    std::vector<Promise<int>> promise_list;
    promise_list.push_back(MkRejectedPromise<int>(Error(Cat(), 0, "err")));
    promise_list.push_back(MkRejectedPromise<int>(Error(Cat(), 1, "err")));
    promise_list.push_back(MkRejectedPromise<int>(Error(Cat(), 2, "err")));

    auto p1 = MkAnyPromise(promise_list, &exec);
    auto p2 = p1.Then(
        [&](Result<int>&& r) -> Result<void> { return r.PassError(); }, &exec);

    exec.Run();

    CATCH_REQUIRE(p1.IsRejected());
    CATCH_REQUIRE(p2.IsRejected());

    auto opt_result = p2.PassResult();
    CATCH_REQUIRE(opt_result);
    CATCH_REQUIRE(!opt_result.value());
    CATCH_REQUIRE(opt_result->GetError().GetMessage() == "no resolved promise");
  }

  {
    MockExecutor exec;

    // the first two promises has not been returned.
    // the last promise has been resolved.
    std::vector<Promise<int>> promise_list;
    promise_list.push_back(Promise<int>{});
    promise_list.push_back(Promise<int>{});
    promise_list.push_back(MkResolvedPromise(111));

    int value = 0;

    auto p = MkRacePromise(promise_list.begin(), promise_list.end(), &exec);
    p.Then(
        [&](Result<int>&& r) -> Result<void> {
          value = r.PassResult();
          return {};
        },
        &exec);

    CATCH_REQUIRE(p.IsEmpty());

    exec.Run();

    CATCH_REQUIRE(p.IsFulfilled());
    CATCH_REQUIRE(value == 111);
  }

  {
    MockExecutor exec;

    std::vector<Promise<int>> promise_list;
    promise_list.push_back(Promise<int>{});
    promise_list.push_back(Promise<int>{});
    promise_list.push_back(MkResolvedPromise(111));

    int value = 0;

    auto p = MkRacePromise(promise_list, &exec);
    p.Then(
        [&](Result<int>&& r) -> Result<void> {
          value = r.PassResult();
          return {};
        },
        &exec);

    CATCH_REQUIRE(p.IsEmpty());

    exec.Run();

    CATCH_REQUIRE(p.IsFulfilled());
    CATCH_REQUIRE(value == 111);
  }

  {
    MockExecutor exec;

    Promise<std::string>::ResolverType r;
    std::vector<std::string> rets;

    auto p =
        MkP<std::string>(&r)
            .ThenAll(
                [&](Result<std::string>&& r)
                    -> Result<std::vector<Promise<std::string>>> {
                  if (!r) return r.PassError();
                  std::string s;
                  std::stringstream ss(r.PassResult());
                  std::vector<Promise<std::string>> promise_list;
                  while (ss >> s) {
                    promise_list.push_back(MkResolvedPromise(std::move(s)));
                  }
                  return promise_list;
                },
                &exec)
            .Then(
                [&](Result<std::vector<std::string>>&& r) -> Result<void> {
                  if (!r) return r.PassError();
                  rets = r.PassResult();
                  return {};
                },
                &exec);

    CATCH_REQUIRE(p.IsEmpty());

    r.Resolve("12 34 56");

    CATCH_REQUIRE(p.IsEmpty());

    exec.Run();

    CATCH_REQUIRE(p.IsFulfilled());
    CATCH_REQUIRE(rets[0] == "12");
    CATCH_REQUIRE(rets[1] == "34");
    CATCH_REQUIRE(rets[2] == "56");
  }

  {
    MockExecutor exec;

    Promise<std::string>::ResolverType r;
    std::vector<std::string> rets;

    auto p =
        MkP<std::string>(&r)
            .ThenAll(
                [&](Result<std::string>&& r)
                    -> Result<std::vector<Promise<std::string>>> {
                  if (!r) return r.PassError();
                  std::string s;
                  std::stringstream ss(r.PassResult());
                  std::vector<Promise<std::string>> promise_list;
                  while (ss >> s) {
                    promise_list.push_back(MkResolvedPromise(std::move(s)));
                  }
                  return promise_list;
                },
                &exec)
            .Then(
                [&](Result<std::vector<std::string>>&& r) -> Result<void> {
                  if (!r) return r.PassError();
                  rets = r.PassResult();
                  return {};
                },
                &exec);

    CATCH_REQUIRE(p.IsEmpty());

    r.Reject(Error(Cat(), 0, "err"));

    CATCH_REQUIRE(p.IsEmpty());

    exec.Run();

    CATCH_REQUIRE(p.IsRejected());
    CATCH_REQUIRE(rets.empty());

    auto opt_result = p.PassResult();
    CATCH_REQUIRE(opt_result);
    CATCH_REQUIRE(!opt_result.value());
    CATCH_REQUIRE(opt_result->GetError().GetMessage() == "err");
  }

  {
    MockExecutor exec;

    std::string ret;
    Promise<std::string>::ResolverType r;

    auto p =
        MkP<std::string>(&r)
            .ThenAny(
                [&](Result<std::string>&& r)
                    -> Result<std::vector<Promise<std::string>>> {
                  if (!r) return r.PassError();
                  std::vector<Promise<std::string>> promise_list;
                  promise_list.push_back(
                      MkRejectedPromise<std::string>(Error(Cat(), 0, "e0")));
                  promise_list.push_back(
                      MkRejectedPromise<std::string>(Error(Cat(), 1, "e1")));
                  promise_list.push_back(MkResolvedPromise(r.PassResult()));
                  return promise_list;
                },
                &exec)
            .Then(
                [&](Result<std::string>&& r) -> Result<void> {
                  if (!r) return r.PassError();
                  ret = r.PassResult();
                  return {};
                },
                &exec);

    CATCH_REQUIRE(p.IsEmpty());

    r.Resolve("456");

    CATCH_REQUIRE(p.IsEmpty());

    exec.Run();

    CATCH_REQUIRE(p.IsFulfilled());
    CATCH_REQUIRE(ret == "456");
  }

  {
    MockExecutor exec;

    std::string ret;
    Promise<std::string>::ResolverType r;

    auto p =
        MkP<std::string>(&r)
            .ThenAny(
                [&](Result<std::string>&& r)
                    -> Result<std::vector<Promise<std::string>>> {
                  if (!r) return r.PassError();
                  std::vector<Promise<std::string>> promise_list;
                  promise_list.push_back(
                      MkRejectedPromise<std::string>(Error(Cat(), 0, "e0")));
                  promise_list.push_back(
                      MkRejectedPromise<std::string>(Error(Cat(), 1, "e1")));
                  promise_list.push_back(MkResolvedPromise(r.PassResult()));
                  return promise_list;
                },
                &exec)
            .Then(
                [&](Result<std::string>&& r) -> Result<void> {
                  if (!r) return r.PassError();
                  ret = r.PassResult();
                  return {};
                },
                &exec);

    CATCH_REQUIRE(p.IsEmpty());

    r.Reject(Error(Cat(), 0, "err"));

    CATCH_REQUIRE(p.IsEmpty());

    exec.Run();

    CATCH_REQUIRE(p.IsRejected());
    CATCH_REQUIRE(ret.empty());
  }

  {
    MockExecutor exec;

    std::string ret;
    Promise<std::string>::ResolverType r;

    auto p =
        MkP<std::string>(&r)
            .ThenRace(
                [&](Result<std::string>&& r)
                    -> Result<std::vector<Promise<std::string>>> {
                  if (!r) return r.PassError();
                  std::vector<Promise<std::string>> promise_list;
                  promise_list.push_back(MkResolvedPromise(r.PassResult()));
                  promise_list.push_back(MkResolvedPromise(std::string{"456"}));
                  return promise_list;
                },
                &exec)
            .Then(
                [&](Result<std::string>&& r) -> Result<void> {
                  if (!r) return r.PassError();
                  ret = r.PassResult();
                  return {};
                },
                &exec);

    CATCH_REQUIRE(p.IsEmpty());

    r.Resolve("123");

    CATCH_REQUIRE(p.IsEmpty());

    exec.Run();

    CATCH_REQUIRE(p.IsFulfilled());
    CATCH_REQUIRE((ret == "123" || ret == "456"));
  }

  {
    MockExecutor exec;

    std::string ret;
    Promise<std::string>::ResolverType r;

    auto p =
        MkP<std::string>(&r)
            .ThenRace(
                [&](Result<std::string>&& r)
                    -> Result<std::vector<Promise<std::string>>> {
                  if (!r) return r.PassError();
                  std::vector<Promise<std::string>> promise_list;
                  promise_list.push_back(MkResolvedPromise(r.PassResult()));
                  promise_list.push_back(MkResolvedPromise(std::string{"456"}));
                  return promise_list;
                },
                &exec)
            .Then(
                [&](Result<std::string>&& r) -> Result<void> {
                  if (!r) return r.PassError();
                  ret = r.PassResult();
                  return {};
                },
                &exec);

    CATCH_REQUIRE(p.IsEmpty());

    r.Reject(Error{Cat(), 0, "err"});

    CATCH_REQUIRE(p.IsEmpty());

    exec.Run();

    CATCH_REQUIRE(p.IsRejected());
    CATCH_REQUIRE(ret.empty());

    auto opt_result = p.PassResult();
    CATCH_REQUIRE(opt_result);
    CATCH_REQUIRE(!opt_result.value());
    CATCH_REQUIRE(opt_result->GetError().GetMessage() == "err");
  }
}

CATCH_TEST_CASE("basic path2", "[Notifier]") {
  {
    MockExecutor exec;

    Notifier notifier;
    auto resolver = notifier.GetResolver();
    CATCH_REQUIRE(std::is_same_v<decltype(resolver), NotifierResolver>);
    CATCH_REQUIRE(notifier.IsEmpty());

    Error v;
    bool run = false;
    notifier.Then(
        [&](Error&& e) -> void {
          run = true;
          v = std::move(e);
        },
        &exec);

    CATCH_REQUIRE(exec.queue.size() == 0);

    resolver.Reject(Error(Cat(), 0, "Failed"));

    CATCH_REQUIRE(exec.queue.size() == 1);
    CATCH_REQUIRE(notifier.IsPreRejected());

    CATCH_REQUIRE(!v);
    CATCH_REQUIRE(!run);

    exec.Run();

    CATCH_REQUIRE(run);
    CATCH_REQUIRE(v);
    CATCH_REQUIRE(v.GetMessage() == "Failed");

    CATCH_REQUIRE(notifier.IsRejected());
  }

  {
    MockExecutor exec;

    Notifier notifier;
    auto resolver = notifier.GetResolver();
    CATCH_REQUIRE(std::is_same_v<decltype(resolver), NotifierResolver>);
    CATCH_REQUIRE(notifier.IsEmpty());

    Error v;
    bool run = false;
    notifier.Then(
        [&](Error&& e) -> void {
          run = true;
          v = std::move(e);
        },
        &exec);

    CATCH_REQUIRE(exec.queue.size() == 0);

    resolver.Resolve();

    CATCH_REQUIRE(exec.queue.size() == 1);
    CATCH_REQUIRE(notifier.IsPreFulfilled());

    CATCH_REQUIRE(!v);
    CATCH_REQUIRE(!run);

    exec.Run();

    CATCH_REQUIRE(run);
    CATCH_REQUIRE(!v);
    CATCH_REQUIRE(notifier.IsFulfilled());
  }
}

}  // namespace event
}  // namespace libz

int main(int argc, char* argv[]) { return Catch::Session().run(argc, argv); }

