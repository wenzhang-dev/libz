#include <event/coroutine.h>
#include <event/io-message-loop.h>
#include <event/promise.h>
#include <grpcpp/grpcpp.h>

#include <thread>

#include "helloworld.grpc.pb.h"

// clang-format off
// protoc --cpp_out=./ *.proto
// protoc --grpc_out=./ --plugin=protoc-gen-grpc=/usr/bin/grpc_cpp_plugin *.proto
// protoc --cpp_out=./ --grpc_out=./ --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` *.proto
// clang-format on

using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using libz::event::IOMessageLoop;
using libz::event::MessageLoop;
using libz::event::MkRejectedNotifier;
using libz::event::MkResolvedNotifier;
using libz::event::Notifier;
using libz::event::Promise;

class ServerImpl final {
   public:
    ServerImpl(MessageLoop* loop, std::uint16_t port) : loop_(loop) {
        Run(port);
    }

    ~ServerImpl() {
        server_->Shutdown();
        // Always shutdown the completion queue after the server.
        cq_->Shutdown();
    }

    // There is no shutdown handling in this code.
    void Run(uint16_t port) {
        std::string server_address = absl::StrFormat("0.0.0.0:%d", port);

        ServerBuilder builder;
        // Listen on the given address without any authentication mechanism.
        builder.AddListeningPort(server_address,
                                 grpc::InsecureServerCredentials());
        // Register "service_" as the instance through which we'll communicate
        // with clients. In this case it corresponds to an *asynchronous*
        // service.
        builder.RegisterService(&service_);
        // Get hold of the completion queue used for the asynchronous
        // communication with the gRPC runtime.
        cq_ = builder.AddCompletionQueue();
        // Finally assemble the server.
        server_ = builder.BuildAndStart();
        std::cout << "Server listening on " << server_address << std::endl;

        Start();
    }

    void Poll() {
        void* got_tag;
        bool ok = false;

        while (cq_->Next(&got_tag, &ok)) {
            loop_->Dispatch(loop_,
                            [this, tag = got_tag] { MakeProgress(tag); });
        }
    }

    void MakeProgress(void* tag) {
        auto s = static_cast<Session*>(tag);
        if (s->state == Session::kRequest) {
            s->Process();
        } else if (s->state == Session::kProcess) {
            s->Finish();
        } else {
            std::cout << "unknown state: " << s->state << std::endl;
        }
    }

   public:
    struct Session {
        enum State { kRequest = 0, kProcess, kFinish };
        State state;

        HelloRequest request;
        std::optional<HelloReply> response;

        Greeter::AsyncService* service;
        ServerContext ctx;
        ServerCompletionQueue* cq;
        ServerAsyncResponseWriter<HelloReply> responder;

        Session(Greeter::AsyncService* as, ServerCompletionQueue* cq)
            : state(kRequest), service(as), ctx(), cq(cq), responder(&ctx) {
            service->RequestSayHello(&ctx, &request, &responder, cq, cq, this);
        }

        Promise<HelloReply> Handle(HelloRequest* req) {
            HelloReply response;

            std::cout << "recv: " << req->name() << std::endl;

            std::string prefix("Hello ");
            response.set_message(prefix + req->name());

            co_return response;
        }

        Notifier Process() {
            Notifier ntfr;
            new Session(service, cq);

            state = Session::kProcess;

            auto resp = co_await Handle(&request);
            if (!resp) {
                auto e = resp.PassError();
                responder.FinishWithError(
                    Status(grpc::StatusCode::INTERNAL, e.GetMessage()), this);
                co_return e;
            }

            response = resp.PassResult();
            responder.Finish(response.value(), Status::OK, this);

            co_return {};
        }

        void Finish() { delete this; }
    };

    void Start() { new Session(&service_, cq_.get()); }

   private:
    MessageLoop* loop_;
    std::unique_ptr<ServerCompletionQueue> cq_;
    Greeter::AsyncService service_;
    std::unique_ptr<Server> server_;
};

int main(int argc, char** argv) {
    IOMessageLoop loop;

    ServerImpl server(&loop, 50051);
    std::thread t(&ServerImpl::Poll, &server);

    loop.Run();
    t.join();

    return 0;
}
