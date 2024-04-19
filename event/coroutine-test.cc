#define CATCH_CONFIG_PREFIX_ALL
#include "coroutine.h"

#include <base/common.h>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <thread>

#include "io-message-loop.h"

namespace libz {
namespace event {

Promise<int> ResolvedPromise() {
  auto res = co_await MkResolvedPromise<int>(123);
  co_return res.PassResult();
}

Promise<int> ResolvedPromiseWrapper() {
  auto res = co_await ResolvedPromise();
  CATCH_REQUIRE(res);
  CATCH_REQUIRE(res.GetResult() == 123);

  co_return res;
}

Promise<int> RejectedPromise() {
  auto res = co_await MkRejectedPromise<int>(Error::MkSysError(1));
  co_return res.PassError();
}

Promise<int> RejectedPromiseWrapper() {
  auto res = co_await RejectedPromise();
  CATCH_REQUIRE(!res);
  CATCH_REQUIRE(res.GetError().code() == 1);

  co_return res;
}

Notifier ResolvedNotifier() {
  auto e = co_await MkResolvedNotifier();

  co_return e;
}

Notifier ResolvedNotifierWrapper() {
  auto e = co_await ResolvedNotifier();
  CATCH_REQUIRE(!e);

  co_return e;
}

Notifier RejectedNotifier() {
  auto e = co_await MkRejectedNotifier(Error::MkSysError(1));
  co_return e;
}

Notifier RejectedNotifierWrapper() {
  auto e = co_await RejectedNotifier();
  CATCH_REQUIRE(e);
  CATCH_REQUIRE(e.code() == 1);

  co_return e;
}

Promise<std::string> ResolvedPromiseChain() {
  Promise<int> p;

  // 100ms timer
  auto current = MessageLoop::Current();
  current->RunAfter(
      [resolver = p.GetResolver()](Error&&) mutable { resolver.Resolve(123); },
      MilliSeconds{100});

  auto p1 = p.Then(
      [](Result<int>&& r) -> Result<std::string> {
        CATCH_REQUIRE(r);
        return std::to_string(r.PassResult());
      },
      current->executor());

  // WARNING: p1 passed by reference, but we can not use it
  auto res = co_await p1;
  CATCH_REQUIRE(res);
  CATCH_REQUIRE(res.GetResult() == "123");

  co_return res;
}

// Notes, the Promise `p` can never be resolved or rejected. So `co_await` is
// always in the suspend state. If we don't cancel the promise manually, the
// coroutine state memory can never be cleaned
Promise<int> CancelledPromise() {
  Promise<int> p;
  auto res = co_await p;

  co_return res;
}

Promise<int> AllResolvedPromise() {
  std::vector<Promise<int>> vec;
  vec.push_back(MkResolvedPromise<int>(1));
  vec.push_back(MkResolvedPromise<int>(2));
  vec.push_back(MkResolvedPromise<int>(3));

  auto current = MessageLoop::Current();
  auto p = MkAllPromise(vec, current->executor());

  auto res = co_await p;
  CATCH_REQUIRE(res);

  int num = 0;
  for (auto& r : res.PassResult()) {
    num += r;
  }
  co_return num;
}

Promise<int> AllResolvedPromise1() {
  Promise<int> p;

  // 100ms timer
  auto current = MessageLoop::Current();
  current->RunAfter(
      [resolver = p.GetResolver()](Error&&) mutable { resolver.Resolve(3); },
      MilliSeconds{100});

  auto p1 = p.ThenAll(
                 [](Result<int>&& r) -> Result<std::vector<Promise<int>>> {
                   CATCH_REQUIRE(r);
                   auto num = r.PassResult();
                   std::vector<Promise<int>> promises;
                   for (auto i = 1; i <= num; ++i) {
                     promises.push_back(MkResolvedPromise<int>(int(i)));
                   }

                   return promises;
                 },
                 current->executor())
                .Then(
                    [](Result<std::vector<int>>&& r) -> Result<int> {
                      CATCH_REQUIRE(r);
                      int num = 0;
                      for (auto& elem : r.PassResult()) {
                        num += elem;
                      }
                      return num;
                    },
                    current->executor());

  auto res = co_await p1;
  CATCH_REQUIRE(res);
  CATCH_REQUIRE(res.GetResult() == 6);

  co_return res;
}

Promise<int> ForLoopCoAwait() {
  std::vector<Promise<int>> promises;
  promises.push_back(MkResolvedPromise<int>(1));
  promises.push_back(MkResolvedPromise<int>(2));
  promises.push_back(MkResolvedPromise<int>(3));

  int num = 0;
  for (auto& p : promises) {
    auto res = co_await p;
    CATCH_REQUIRE(res);
    num += res.PassResult();

    std::cout << "num: " << num << std::endl;
  }

  CATCH_REQUIRE(num == 6);

  co_return num;
}

Promise<int> ThrowException() {
  throw std::bad_alloc();
  co_return 1;
}

CATCH_TEST_CASE("basic", "[coroutine]") {
  MessageLoop* loop;
  std::thread t([&loop] {
    IOMessageLoop io_loop;
    loop = &io_loop;

    io_loop.Run();
  });

  DEFER([&] {
    loop->Shutdown();
    t.join();
  });

  std::this_thread::sleep_for(Seconds(3));

  std::atomic<std::size_t> cases = 0;
  std::vector<Promise<int>> promises;
  std::vector<Promise<std::string>> string_promises;

  std::vector<Notifier> notifiers;

  // case 1:
  loop->Post([&]() {
    promises.emplace_back(ResolvedPromiseWrapper());
    promises.back().Then(
        [&](Result<int>&& r) {
          CATCH_REQUIRE(r);
          CATCH_REQUIRE(r.PassResult() == 123);
          ++cases;
        },
        loop->executor());
  });

  // case 2:
  loop->Post([&]() {
    promises.emplace_back(RejectedPromiseWrapper());
    promises.back().Then(
        [&](Result<int>&& r) {
          CATCH_REQUIRE(!r);
          CATCH_REQUIRE(r.PassError().code() == 1);
          ++cases;
        },
        loop->executor());
  });

  // case 3:
  loop->Post([&]() {
    notifiers.emplace_back(ResolvedNotifierWrapper());
    notifiers.back().Then(
        [&](Error&& e) {
          CATCH_REQUIRE(!e);
          ++cases;
        },
        loop->executor());
  });

  // case 4:
  loop->Post([&]() {
    notifiers.emplace_back(RejectedNotifierWrapper());
    notifiers.back().Then(
        [&](Error&& e) {
          CATCH_REQUIRE(e);
          CATCH_REQUIRE(e.code() == 1);
          ++cases;
        },
        loop->executor());
  });

  // case 5:
  loop->Post([&]() {
    string_promises.emplace_back(ResolvedPromiseChain());
    string_promises.back().Then(
        [&](Result<std::string>&& r) {
          CATCH_REQUIRE(r);
          CATCH_REQUIRE(r.GetResult() == "123");
          ++cases;
        },
        loop->executor());
  });

  // case 6:
  loop->Post([&]() {
    promises.emplace_back(CancelledPromise());
    // If we don't cancel the promise manually, the ASAN will complain abort
    // memory leaks
    promises.back().Cancel();
    ++cases;
  });

  // case 7:
  loop->Post([&]() {
    promises.emplace_back(AllResolvedPromise());
    promises.back().Then(
        [&](Result<int>&& r) {
          CATCH_REQUIRE(r);
          CATCH_REQUIRE(r.PassResult() == 6);
          ++cases;
        },
        loop->executor());
  });

  // case 8:
  loop->Post([&]() {
    promises.emplace_back(AllResolvedPromise1());
    promises.back().Then(
        [&](Result<int>&& r) {
          CATCH_REQUIRE(r);
          CATCH_REQUIRE(r.PassResult() == 6);
          ++cases;
        },
        loop->executor());
  });

  // case 9:
  loop->Post([&]() {
    promises.emplace_back(ForLoopCoAwait());
    promises.back().Then(
        [&](Result<int>&& r) {
          CATCH_REQUIRE(r);
          CATCH_REQUIRE(r.PassResult() == 6);
          ++cases;
        },
        loop->executor());
  });

  // case 10:
  loop->Post([&]() {
    promises.emplace_back(ThrowException());
    promises.back().Then(
        [&](Result<int>&& r) {
          CATCH_REQUIRE(!r);
          std::cout << r.GetError().GetMessage() << std::endl;
          CATCH_REQUIRE(r.PassError().code() == kErrorCoroutineException);
          ++cases;
        },
        loop->executor());
  });

  // TODO: race, any promise

  for (auto i = 0; i < 30; i++) {
    if (cases.load() != 10) {
      std::this_thread::sleep_for(MilliSeconds(500));
    }
  }
  CATCH_REQUIRE(cases.load() == 10);
}

}  // namespace event
}  // namespace libz

int main(int argc, char* argv[]) { return Catch::Session().run(argc, argv); }

