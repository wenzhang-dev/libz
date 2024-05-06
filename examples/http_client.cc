#include <ada.h>
#include <base/error.h>
#include <base/result.h>
#include <control/io-thread.h>
#include <event/coroutine.h>
#include <event/io-message-loop.h>

#include <asio.hpp>

using asio::ip::tcp;
using libz::Error;
using libz::MilliSeconds;
using libz::Result;
using libz::ctl::IOThread;
using libz::ctl::IOThreadPool;
using libz::event::IOMessageLoop;
using libz::event::MessageLoop;
using libz::event::MkRejectedPromise;
using libz::event::Notifier;
using libz::event::Promise;
using libz::event::TimerToken;

template <typename Resolver>
bool HandleError(const asio::error_code& ec, Resolver& resolver) {
  if (auto done = resolver.IsSettled(); done && done.value()) {
    return true;
  }

  if (ec) {
    resolver.Reject(Error::MkBoostError(ec.value(), ec.message()));
    return true;
  }

  return false;
}

class Resolver {
 public:
  Resolver(MessageLoop* loop) : loop_(loop) {}

  Resolver(Resolver&&) = default;
  Resolver(const Resolver&) = default;
  Resolver& operator=(Resolver&&) = default;
  Resolver& operator=(const Resolver&) = default;

 public:
  using IPAddressList = std::vector<std::string>;

  Promise<IPAddressList> Resolve(std::string_view host,
                                 std::optional<MilliSeconds> timeout = {}) {
    Promise<IPAddressList> promise;
    auto resolver = std::make_shared<tcp::resolver>(*loop_->proactor());

    auto handler = [_ = resolver, resolver = promise.GetResolver()](
                       const asio::error_code& ec,
                       tcp::resolver::iterator it) mutable {
      if (HandleError(ec, resolver)) {
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
    };

    if (timeout) {
      auto token = loop_->AddTimerEvent(
          [promise_resolver = promise.GetResolver(),
           weak_resolver = std::weak_ptr(resolver)](Error&& e) mutable {
            UNUSE(e);
            if (auto resolver = weak_resolver.lock(); resolver) {
              resolver->cancel();
              promise_resolver.Reject(
                  Error::MkGeneralError(-1, "cancel", "net"));
            }
          },
          *timeout);

      resolver->async_resolve(
          host, "",
          [handler = std::move(handler), cancelable = token.AsCancelable()](
              const asio::error_code& ec, tcp::resolver::iterator it) mutable {
            cancelable.reset();
            handler(ec, it);
          });
    } else {
      resolver->async_resolve(host, "", std::move(handler));
    }

    return promise;
  }

 private:
  MessageLoop* loop_;
};

template <typename socket_type>
struct ClientBase {
  struct Body : std::istream {
    asio::streambuf& buf;

    Body(asio::streambuf& buf) : std::istream(&buf), buf(buf) {}
    std::size_t size() noexcept { return buf.size(); }

    std::string string() noexcept {
      try {
        std::string str;
        auto size = buf.size();
        str.resize(size);
        read(&str[0], static_cast<std::streamsize>(size));
        return str;
      } catch (...) {
        return std::string();
      }
    }
  };

  using Headers = std::unordered_map<std::string, std::string>;
  struct Response {
    std::string status;
    std::string http_version;
    Headers headers;

    std::unique_ptr<asio::streambuf> streambuf;
    std::unique_ptr<Body> body;

    Response()
        : streambuf(std::make_unique<asio::streambuf>()),
          body(std::make_unique<Body>(*streambuf)) {}
  };

  struct Connection : std::enable_shared_from_this<Connection> {
    std::unique_ptr<socket_type> socket;

    template <typename... Args>
    Connection(Args&&... args)
        : socket(std::make_unique<socket_type>(std::forward<Args>(args)...)) {}
  };

  using ConnectionPtr = std::shared_ptr<Connection>;

  std::unique_ptr<asio::streambuf> PrepareRequest(std::string_view method,
                                                  std::string_view path,
                                                  const Headers& headers) {
    auto stream_buf = std::make_unique<asio::streambuf>();
    std::ostream write_stream(stream_buf.get());
    write_stream << method << " " << path << " HTTP/1.1\r\n";
    for (const auto& h : headers) {
      write_stream << h.first << ": " << h.second << "\r\n";
    }

    return stream_buf;
  }

  Error ParseResponse(std::istream& stream, std::string& version,
                      std::string& status_code, Headers& headers) {
    headers.clear();
    std::string line;
    getline(stream, line);
    auto version_end = line.find(' ');
    if (version_end == std::string::npos) {
      return Error::MkGeneralError(-1, "invalid response", "net");
    }

    if (line.size() < 5) {
      return Error::MkGeneralError(-1, "invalid response", "net");
    } else {
      version = line.substr(5, version_end - 5);
    }

    if (version_end + 1 > line.size()) {
      return Error::MkGeneralError(-1, "invalid response", "net");
    } else {
      status_code =
          line.substr(version_end + 1, line.size() - (version_end + 1) - 1);
    }

    getline(stream, line);
    std::size_t param_end;
    while ((param_end = line.find(':')) != std::string::npos) {
      std::size_t value_start = param_end + 1;
      while (value_start + 1 < line.size() && line[value_start] == ' ') {
        ++value_start;
      }
      if (value_start < line.size()) {
        headers.emplace(
            line.substr(0, param_end),
            line.substr(value_start, line.size() - value_start - 1));
      }

      getline(stream, line);
    }

    return {};
  }

  struct Session {
    ConnectionPtr connection;

    std::unique_ptr<asio::streambuf> request_streambuf;
    Response response;

    Session(ConnectionPtr conn, std::unique_ptr<asio::streambuf>&& request)
        : connection(std::move(conn)), request_streambuf(std::move(request)) {}
  };

  Promise<ConnectionPtr> Connect(std::string_view ip, short port,
                                 std::optional<MilliSeconds> timeout = {}) {
    Promise<ConnectionPtr> promise;
    auto ep = tcp::endpoint(asio::ip::make_address(ip), port);
    auto conn = std::make_shared<Connection>(*loop->proactor());

    auto handler = [resolver = promise.GetResolver(),
                    conn](const asio::error_code& ec) mutable {
      if (HandleError(ec, resolver)) {
        return;
      }
      resolver.Resolve(std::move(conn));
    };

    auto socket_ptr = conn->socket.get();
    if (timeout) {
      auto token = loop->AddTimerEvent(
          [resolver = promise.GetResolver(),
           weak_conn = std::weak_ptr(conn)](Error&& e) mutable {
            UNUSE(e);
            if (auto conn = weak_conn.lock(); conn) {
              auto socket_ptr = conn->socket.get();
              socket_ptr->cancel();
              resolver.Reject(Error::MkGeneralError(-1, "cancel", "net"));
            }
          },
          *timeout);

      socket_ptr->async_connect(
          ep, [handler = std::move(handler), cancelable = token.AsCancelable()](
                  const asio::error_code& ec) mutable {
            cancelable.reset();
            handler(ec);
          });
    } else {
      socket_ptr->async_connect(ep, std::move(handler));
    }

    return promise;
  }

  Promise<std::size_t> WriteRequest(const std::shared_ptr<Session>& sess,
                                    std::optional<MilliSeconds> timeout = {}) {
    Promise<std::size_t> promise;
    auto handler = [sess, resolver = promise.GetResolver()](
                       const asio::error_code& ec, std::size_t bytes) mutable {
      if (HandleError(ec, resolver)) {
        return;
      }
      resolver.Resolve(bytes);
    };

    auto socket_ptr = sess->connection->socket.get();
    if (timeout) {
      auto token = loop->AddTimerEvent(
          [resolver = promise.GetResolver(),
           weak_sess = std::weak_ptr(sess)](Error&& e) mutable {
            UNUSE(e);
            if (auto sess = weak_sess.lock(); sess) {
              auto socket_ptr = sess->connection->socket.get();
              socket_ptr->cancel();
              resolver.Reject(Error::MkGeneralError(-1, "cancel", "net"));
            }
          },
          *timeout);

      asio::async_write(
          *socket_ptr, sess->request_streambuf->data(),
          [handler = std::move(handler), cancelable = token.AsCancelable()](
              const asio::error_code& ec, std::size_t bytes) mutable {
            cancelable.reset();
            handler(ec, bytes);
          });

    } else {
      asio::async_write(*socket_ptr, sess->request_streambuf->data(),
                        std::move(handler));
    }

    return promise;
  }

  Promise<Response> ReadResponse(const std::shared_ptr<Session>& sess,
                                 std::optional<MilliSeconds> timeout = {}) {
    Promise<Response> promise;

    auto handler = [this, sess, resolver = promise.GetResolver()](
                       const asio::error_code& ec, std::size_t bytes) mutable {
      if (HandleError(ec, resolver)) {
        return;
      }

      auto additional_bytes = sess->response.streambuf->size() - bytes;

      if (auto e =
              ParseResponse(*sess->response.body, sess->response.http_version,
                            sess->response.status, sess->response.headers);
          e) {
        resolver.Reject(std::move(e));
        return;
      }

      auto length_it = sess->response.headers.find("Content-Length");
      assert(length_it != sess->response.headers.end());

      auto content_length = std::stoull(length_it->second);
      if (content_length < additional_bytes) {
        resolver.Reject(Error::MkGeneralError(-1, "invalid response", "net"));
      } else {
        auto socket_ptr = sess->connection->socket.get();
        asio::async_read(
            *socket_ptr, *sess->response.streambuf,
            asio::transfer_exactly(content_length - additional_bytes),
            [sess, resolver = std::move(resolver)](const asio::error_code& ec,
                                                   std::size_t) mutable {
              if (HandleError(ec, resolver)) {
                return;
              }

              resolver.Resolve(std::move(sess->response));
            });
      }
    };

    auto socket_ptr = sess->connection->socket.get();
    if (timeout) {
      auto token = loop->AddTimerEvent(
          [resolver = promise.GetResolver(),
           weak_sess = std::weak_ptr(sess)](Error&& e) mutable {
            UNUSE(e);
            if (auto sess = weak_sess.lock(); sess) {
              auto socket_ptr = sess->connection->socket.get();
              socket_ptr->cancel();
              resolver.Reject(Error::MkGeneralError(-1, "cancel", "net"));
            }
          },
          *timeout);

      asio::async_read_until(
          *socket_ptr, *sess->response.streambuf, "\r\n\r\n",
          [handler = std::move(handler), cancelable = token.AsCancelable()](
              const asio::error_code& ec, std::size_t bytes) mutable {
            cancelable.reset();
            handler(ec, bytes);
          });

    } else {
      asio::async_read_until(*socket_ptr, *sess->response.streambuf, "\r\n\r\n",
                             std::move(handler));
    }

    return promise;
  }

  struct Options {
    std::optional<MilliSeconds> dns_timeout;
    std::optional<MilliSeconds> connect_timeout;
    std::optional<MilliSeconds> send_timeout;
    std::optional<MilliSeconds> receive_timeout;
  };

  Promise<Response> Request(std::string_view method, std::string_view url,
                            Headers&& headers = {},
                            std::optional<std::string_view> data = {},
                            Options&& opts = {}) {
    short port;
    std::string host;
    std::string path;
    if (auto parsed_url = ada::parse<ada::url_aggregator>(url); !parsed_url) {
      co_return Error::MkGeneralError(-1, "invalid url", "net");
    } else {
      host = parsed_url.value().get_hostname();
      port = parsed_url.value().get_special_port();
      // TODO: need path
      path = parsed_url.value().get_pathname();
    }

    std::cout << "host: " << host << std::endl;
    auto dns_result = co_await dns_resolver->Resolve(host, opts.dns_timeout);
    if (!dns_result) {
      co_return dns_result.PassError();
    }

    auto ip_list = dns_result.PassResult();
    if (ip_list.empty()) {
      co_return Error::MkGeneralError(-1, "invalid ip", "net");
    }

    auto conn_result = co_await Connect(ip_list[0], port, opts.connect_timeout);
    if (!conn_result) {
      co_return conn_result.PassError();
    }

    auto conn_ptr = conn_result.PassResult();
    auto session = std::make_shared<Session>(
        std::move(conn_ptr), PrepareRequest(method, path, headers));

    std::ostream write_stream(session->request_streambuf.get());
    if (headers.find("Host") == headers.end()) {
      write_stream << "Host: " << host << "\r\n";
    }
    if (data && data->size() > 0) {
      if (headers.find("Content-Length") == headers.end()) {
        write_stream << "Content-Length: " << data->size() << "\r\n";
      }
    }
    write_stream << "\r\n";
    if (data) {
      write_stream << *data;
    }

    co_await WriteRequest(session, opts.send_timeout);

    auto response_result = co_await ReadResponse(session, opts.receive_timeout);
    if (!response_result) {
      co_return response_result.PassError();
    }
    co_return response_result.PassResult();
  }

  ClientBase(MessageLoop* loop)
      : loop(loop), dns_resolver(std::make_unique<Resolver>(loop)) {}

  MessageLoop* loop;
  std::unique_ptr<Resolver> dns_resolver;
};

using HttpClient = ClientBase<tcp::socket>;

int main() {
  IOMessageLoop loop;

  HttpClient client(&loop);

  loop.Post([&client]() {
    auto handler = [&client]() -> Notifier {
      auto response_result = co_await client.Request("GET", "http://baidu.com");
      assert(response_result);
      auto response = response_result.PassResult();
      std::cout << "status_code: " << response.status << std::endl;
      std::cout << "body: " << response.body->string() << std::endl;

      co_return {};
    };

    handler();
  });

  loop.Run();

  return 0;
}
