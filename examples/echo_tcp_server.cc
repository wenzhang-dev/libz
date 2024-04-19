#include <base/error.h>
#include <event/coroutine.h>
#include <event/io-message-loop.h>

#include <asio.hpp>

using asio::ip::tcp;
using libz::Error;
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

Promise<std::size_t> WriteSocket(std::shared_ptr<tcp::socket> sock, const std::string& buffer) {
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

class server {
   public:
    server(MessageLoop* loop, short port)
        : loop_(loop),
          acceptor_(*loop->proactor(), tcp::endpoint(tcp::v4(), port)) {
        do_accept();
    }

   private:
    void do_accept() {
        std::cout << "acceptor routine" << std::endl;
        acceptor_.async_accept([this](const asio::error_code& ec,
                                      tcp::socket&& socket) {
            if (!ec) {
                std::cout << "new socket" << std::endl;
                auto shared_sock =
                    std::make_shared<tcp::socket>(std::move(socket));
                loop_->Post(
                    [socket = std::move(shared_sock)]() mutable -> void {
                        EchoRoutine(socket);
                    });
            }

            do_accept();
        });
    }

    MessageLoop* loop_;
    tcp::acceptor acceptor_;
};

int main() {
    IOMessageLoop loop;

    server s(&loop, 8080);

    loop.Run();

    return 0;
}
