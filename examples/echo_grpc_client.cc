#include <event/coroutine.h>
#include <event/io-message-loop.h>
#include <event/promise.h>
#include <grpcpp/grpcpp.h>

#include <thread>

#include "helloworld2.grpc.pb.h"

// clang-format off
// protoc --cpp_out=./ *.proto
// protoc --grpc_out=./ --plugin=protoc-gen-grpc=/usr/bin/grpc_cpp_plugin *.proto
// protoc --cpp_out=./ --grpc_out=./ --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` *.proto
// clang-format on

using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;
using helloworld2::EchoReply;
using helloworld2::EchoRequest;
using helloworld2::Greeter;
using helloworld2::HelloReply;
using helloworld2::HelloRequest;

using libz::Error;
using libz::event::IOMessageLoop;
using libz::event::MessageLoop;
using libz::event::MkRejectedNotifier;
using libz::event::MkResolvedNotifier;
using libz::event::Notifier;
using libz::event::Promise;

struct SessionBase {
    virtual void Finish() = 0;
    virtual ~SessionBase() {}
};

template <typename Req, typename Rsp>
struct Session : public SessionBase {
    Req request;
    Rsp response;

    Status status;
    ClientContext context;
    CompletionQueue* cq;
    std::unique_ptr<ClientAsyncResponseReader<Rsp>> reader;
    std::optional<typename Promise<Rsp>::ResolverType> resolver;

    Session(Req&& req, CompletionQueue* cq)
        : SessionBase(), request(std::move(req)), cq(cq) {}

    void Finish() override {
        assert(resolver);

        if (status.ok()) {
            resolver->Resolve(std::move(response));
        } else {
            resolver->Reject(Error::MkBoostError(status.error_code(),
                                                 status.error_message()));
        }
    }

    Promise<Rsp> Request() {
        Promise<Rsp> p;

        assert(reader);
        reader->Finish(&response, &status, this);
        resolver = p.GetResolver();

        return p;
    }
};

struct HelloSession : public Session<HelloRequest, HelloReply> {
    HelloSession(Greeter::Stub* stub, HelloRequest&& req, CompletionQueue* cq)
        : Session(std::move(req), cq) {
        reader = std::unique_ptr<ClientAsyncResponseReader<HelloReply>>(
            stub->AsyncSayHello(&context, request, cq));
    }
};
struct EchoSession : public Session<EchoRequest, EchoReply> {
    EchoSession(Greeter::Stub* stub, EchoRequest&& req, CompletionQueue* cq)
        : Session(std::move(req), cq) {
        reader = std::unique_ptr<ClientAsyncResponseReader<EchoReply>>(
            stub->AsyncSayEcho(&context, request, cq));
    }
};

class GreeterClient {
   public:
    explicit GreeterClient(MessageLoop* loop, std::shared_ptr<Channel> channel)
        : loop_(loop),
          stub_(Greeter::NewStub(channel)),
          cq_(std::make_unique<CompletionQueue>()) {}

    void Poll() {
        void* tag;
        bool ok = false;

        while (cq_->Next(&tag, &ok)) {
            loop_->Dispatch(loop_, [this, tag] { MakeProgress(tag); });
        }
    }

    void MakeProgress(void* tag) {
        auto s = static_cast<SessionBase*>(tag);
        s->Finish();
    }

    Promise<HelloReply> SayHello(HelloRequest&& request) {
        HelloSession s(stub_.get(), std::move(request), cq_.get());
        auto response = co_await s.Request();
        co_return response;
    }

    Promise<EchoReply> SayEcho(EchoRequest&& request) {
        EchoSession s(stub_.get(), std::move(request), cq_.get());
        auto response = co_await s.Request();
        co_return response;
    }

   private:
    MessageLoop* loop_;
    std::unique_ptr<Greeter::Stub> stub_;
    std::unique_ptr<CompletionQueue> cq_;
};

int main(int argc, char** argv) {
    IOMessageLoop loop;

    GreeterClient greeter(
        &loop, grpc::CreateChannel("localhost:50051",
                                   grpc::InsecureChannelCredentials()));

    std::thread t(&GreeterClient::Poll, &greeter);

    loop.Post([&greeter] {
        auto handler = [&greeter]() -> Notifier {
            HelloRequest req;
            req.set_name("world");
            auto response = co_await greeter.SayHello(std::move(req));
            assert(response);
            std::cout << response.PassResult().message() << std::endl;
            co_return {};
        };
        handler();
    });

    loop.Post([&greeter] {
        auto handler = [&greeter]() -> Notifier {
            EchoRequest req;
            req.set_content("world");
            auto response = co_await greeter.SayEcho(std::move(req));
            assert(response);
            std::cout << response.PassResult().content() << std::endl;
            co_return {};
        };
        handler();
    });

    loop.Run();
    t.join();

    return 0;
}
