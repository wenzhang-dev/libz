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

Promise<std::pair<bool, size_t>> ReadSocket(std::shared_ptr<tcp::socket> sock,
                                            std::string& buffer) {
  Promise<std::pair<bool, size_t>> p;

  sock->async_read_some(
      asio::buffer(buffer.data(), buffer.size()),
      [resolver = p.GetResolver()](const asio::error_code& ec,
                                   std::size_t length) mutable {
        if (ec == asio::error::eof) {
          std::cout << "read err: eof" << std::endl;
          resolver.Resolve(std::make_pair(true, length));
        } else if (ec) {
          std::cout << "read err:" << ec.message() << std::endl;
          resolver.Reject(Error::MkBoostError(ec.value(), ec.message()));
        }

        resolver.Resolve(std::make_pair(false, length));
      });

  return p;
}

Promise<std::size_t> WriteSocket(std::shared_ptr<tcp::socket> sock,
                                 const std::string& buffer) {
  Promise<std::size_t> p;

  sock->async_write_some(
      asio::buffer(buffer.data(), buffer.size()),
      [resolver = p.GetResolver()](const asio::error_code& ec,
                                   std::size_t length) mutable {
        if (ec) {
          std::cout << "write err: " << ec.message() << std::endl;
          resolver.Reject(Error::MkBoostError(ec.value(), ec.message()));
        }

        resolver.Resolve(length);
      });

  return p;
}

Promise<size_t> EchoRoutine(std::shared_ptr<tcp::socket> sock) {
  std::size_t count{};

  std::string buffer;

  for (;;) {
    buffer.resize(1024);
    auto read_result = co_await ReadSocket(sock, buffer);
    if (!read_result) {
      break;
    }

    auto [eof, read_length] = read_result.PassResult();

    if (eof || read_length == 0) {
      break;
    }

    buffer.resize(read_length);
    std::cout << "recv: " << buffer << std::endl;

    auto write_result = co_await WriteSocket(sock, buffer);
    if (!write_result) {
      break;
    }

    ++count;
  }

  co_return count;
}

class Acceptor {
 public:
  Acceptor(MessageLoop* loop, short port)
      : loop_(loop),
        acceptor_(*loop->proactor(), tcp::endpoint(tcp::v4(), port)) {}

  // Support for specifying the MessageLoop
  Promise<std::shared_ptr<tcp::socket>> Accept(MessageLoop* loop = nullptr) {
    if (loop == nullptr) {
      loop = loop_;
    }

    Promise<std::shared_ptr<tcp::socket>> promise;
    auto socket = std::make_shared<tcp::socket>(*loop->proactor());
    acceptor_.async_accept(*socket, [socket, resolver = promise.GetResolver()](
                                        const asio::error_code& ec) mutable {
      if (ec) {
        resolver.Reject(Error::MkBoostError(ec.value(), ec.message()));
      }
      resolver.Resolve(std::move(socket));
    });

    return promise;
  }

 private:
  MessageLoop* loop_;
  tcp::acceptor acceptor_;
};

class Server {
 public:
  Server(short port, std::size_t pool_size = 4)
      : loop_(std::make_unique<IOMessageLoop>()),
        acceptor_(std::make_unique<Acceptor>(loop_.get(), port)),
        pool_(std::make_unique<IOThreadPool>(pool_size)),
        socket_promise_() {}

  void Run() {
    StartAccept();

    pool_->Run();
    loop_->Run();
  }

  void Shutdown() {
    loop_->Shutdown();
    pool_->Shutdown();
  }

  MessageLoop* event_loop() { return loop_.get(); }

 private:
  void StartAccept() {
    auto idx = std::rand() % pool_->MaxIOThread();
    auto io_thread = pool_->At(idx);

    socket_promise_ = acceptor_->Accept(io_thread->event_loop());
    socket_promise_->Then(
        [this, idx, io_thread](Result<std::shared_ptr<tcp::socket>>&& r) {
          if (r) {
            std::cout << "new socket#" << idx << std::endl;
            loop_->Dispatch(io_thread->event_loop(),
                            [this, socket = r.PassResult()]() mutable {
                              HandleAccept(std::move(socket));
                            });
          } else {
            std::cout << "acceptor err: " << r.PassError().Details()
                      << std::endl;
          }

          StartAccept();
        },
        loop_->executor());
  }

  void HandleAccept(std::shared_ptr<tcp::socket>&& socket) {
    EchoRoutine(std::move(socket));
  }

  std::unique_ptr<MessageLoop> loop_;

  std::unique_ptr<Acceptor> acceptor_;
  std::unique_ptr<IOThreadPool> pool_;

  std::optional<Promise<std::shared_ptr<tcp::socket>>> socket_promise_;
};

int main() {
  Server s(18080);
  s.Run();

  return 0;
}
