#include <base/error.h>
#include <base/result.h>
#include <control/io-thread.h>
#include <event/coroutine.h>
#include <event/io-message-loop.h>

#include <asio.hpp>

using asio::ip::tcp;
using libz::Error;
using libz::Result;
using libz::ctl::IOThread;
using libz::ctl::IOThreadPool;
using libz::event::IOMessageLoop;
using libz::event::MessageLoop;
using libz::event::Notifier;
using libz::event::Promise;

class Resolver {
 public:
  Resolver(MessageLoop* loop) : loop_(loop) {}

  Resolver(Resolver&&) = default;
  Resolver(const Resolver&) = default;
  Resolver& operator=(Resolver&&) = default;
  Resolver& operator=(const Resolver&) = default;

 public:
  using IPAddressList = std::vector<std::string>;

  Promise<IPAddressList> Resolve(const std::string& host) {
    Promise<IPAddressList> promise;
    auto resolver = std::make_shared<tcp::resolver>(*loop_->proactor());

    resolver->async_resolve(
        host, "",
        [_ = resolver, resolver = promise.GetResolver()](
            const asio::error_code& ec, tcp::resolver::iterator it) mutable {
          if (ec) {
            resolver.Reject(Error::MkBoostError(ec.value(), ec.message()));
            return;
          }

          asio::error_code err;
          IPAddressList addr_list;
          tcp::resolver::iterator end;
          while (it != end) {
            if (auto ip = it->endpoint().address().to_string(err); !err) {
              addr_list.push_back(std::move(ip));
            }

            ++it;
          }

          resolver.Resolve(std::move(addr_list));
        });

    return promise;
  }

 private:
  MessageLoop* loop_;
};

int main() {
  IOMessageLoop loop;

  loop.Post([] {
    auto handler = []() -> Notifier {
      auto loop = MessageLoop::Current();

      Resolver resolver(loop);
      auto host = "baidu.com";
      auto ip_result = co_await resolver.Resolve(host);
      assert(ip_result);

      auto ip_list = ip_result.PassResult();

      std::cout << "host: " << host << std::endl;
      std::for_each(ip_list.begin(), ip_list.end(),
                    [](const auto& ip) { std::cout << ip << std::endl; });

      co_return {};
    };
    handler();
  });

  loop.Run();

  return 0;
}
