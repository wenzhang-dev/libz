#include <base/common.h>
#include <event/coroutine.h>
#include <event/io-message-loop.h>
#include <event/promise.h>
#include <fcntl.h>
#include <liburing.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <thread>

using libz::Error;
using libz::_::TransactionRunner;
using libz::event::IOMessageLoop;
using libz::event::MessageLoop;
using libz::event::MkRejectedPromise;
using libz::event::Notifier;
using libz::event::Promise;

template <typename T, std::size_t alignment>
class AlignedAllocator {
 public:
  using value_type = T;

 public:
  AlignedAllocator() noexcept {};

  template <typename U>
  AlignedAllocator(const AlignedAllocator<U, alignment>& other) noexcept {};

  template <typename U>
  inline bool operator==(
      const AlignedAllocator<U, alignment>& other) const noexcept {
    return true;
  }

  template <typename U>
  inline bool operator!=(
      const AlignedAllocator<U, alignment>& other) const noexcept {
    return false;
  }

  template <typename U>
  struct rebind {
    using other = AlignedAllocator<U, alignment>;
  };

  inline value_type* allocate(const std::size_t n) const {
    auto size = n;
    value_type* ptr;
    auto ret = ::posix_memalign((void**)&ptr, alignment, sizeof(T) * size);
    if (ret != 0) throw std::bad_alloc();
    return ptr;
  };

  inline void deallocate(value_type* const ptr, std::size_t n) const noexcept {
    ::free(ptr);
  }
};

using AlignedString = std::basic_string<char, std::char_traits<char>,
                                        AlignedAllocator<char, 4096>>;

struct IOUring {
  std::unique_ptr<::io_uring> ring;

  IOUring(std::size_t depth) : ring(std::make_unique<::io_uring>()) {
    ::io_uring_queue_init(depth, ring.get(), 0);
  }

  ~IOUring() { ::io_uring_queue_exit(ring.get()); }
};

struct IOContextBase {
  virtual ~IOContextBase() {}
  virtual void Finish(int res) = 0;
};

template <typename T>
struct IOContext : public IOContextBase {
  using ValueType = T;

  int fd;
  ::io_uring* ring;
  std::optional<typename Promise<T>::ResolverType> resolver;
  T data;

  ~IOContext() { ::close(fd); }

  const T* Data() const { return &data; }
  T* Data() { return &data; }

  IOContext(::io_uring* ring, int fd,
            typename Promise<T>::ResolverType&& resolver, T&& data = {})
      : fd(fd),
        ring(ring),
        resolver(std::move(resolver)),
        data(std::move(data)) {}
};

struct ReadIOContext : public IOContext<AlignedString> {
  using Base = IOContext<AlignedString>;

  static ReadIOContext* New(
      ::io_uring* ring, int fd,
      typename Promise<Base::ValueType>::ResolverType&& resolver,
      Base::ValueType&& data = {}) {
    return new ReadIOContext(ring, fd, std::move(resolver), std::move(data));
  }
  void Finish(int res) override {
    if (res < 0) {
      resolver->Reject(Error::MkGeneralError(res, strerror(-res), "finish"));
    } else {
      data.resize(res);
      resolver->Resolve(std::move(data));
    }

    delete this;
  }

  ::iovec* PrepareBuffer(std::size_t length) {
    Data()->resize(legnth);

    iov.iov_base = Data()->data();
    iov.iov_len = length;

    return &iov;
  }

 private:
  ReadIOContext(::io_uring* ring, int fd,
                typename Promise<Base::ValueType>::ResolverType&& resolver,
                Base::ValueType&& data = {})
      : Base(ring, fd, std::move(resolver), std::move(data)) {}

  ::iovec iov;
};

Promise<AlignedString> ReadFile(IOUring* uring, std::string_view file) {
  auto fd = ::open(file.data(), O_RDONLY | O_DIRECT);
  if (fd < 0) {
    return MkRejectedPromise<AlignedString>(Error::MkSysError(errno));
  }

  Promise<AlignedString> promise;
  auto io_ctx =
      ReadIOContext::New(uring->ring.get(), fd, promise.GetResolver());

  TransactionRunner runner([&io_ctx]() { delete io_ctx; });

  auto sqe = ::io_uring_get_sqe(uring->ring.get());
  if (!sqe) {
    return MkRejectedPromise<AlignedString>(
        Error::MkGeneralError(-1, "get_sqe failed", "read_file"));
  }
  ::io_uring_sqe_set_data(sqe, io_ctx);

  auto iov = io_ctx->PrepareBuffer(4096);
  ::io_uring_prep_readv(sqe, fd, iov, 1, 0);

  auto ret = ::io_uring_submit(uring->ring.get());
  if (ret < 0 || ret != 1) {
    return MkRejectedPromise<AlignedString>(
        Error::MkGeneralError(ret, "submit failed", "read_file"));
  }

  runner.Commit();
  return promise;
}

void Poll(IOUring* uring) {
  for (;;) {
    ::io_uring_cqe* cqe;
    auto ret = ::io_uring_wait_cqe(uring->ring.get(), &cqe);
    if (ret < 0) {
      std::cout << "wait_cqe failed: " << strerror(-ret) << std::endl;
      break;
    }

    std::cout << "receive a cqe" << std::endl;

    reinterpret_cast<IOContextBase*>(::io_uring_cqe_get_data(cqe))
        ->Finish(cqe->res);

    ::io_uring_cqe_seen(uring->ring.get(), cqe);
  }
}

int main() {
  IOMessageLoop loop;
  IOUring uring(4);

  std::thread t(Poll, &uring);

  loop.Post([&uring]() {
    auto handler = [&uring]() -> Notifier {
      auto result = co_await ReadFile(&uring, "data.txt");
      assert(result);
      std::cout << "content: " << result.PassResult() << std::endl;
      co_return {};
    };
    handler();
  });

  loop.Run();
  t.join();

  return 0;
}
