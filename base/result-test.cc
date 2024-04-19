#define CATCH_CONFIG_PREFIX_ALL
#include "result.h"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>

namespace libz {

class ErrorCategory : public Error::Category {
  const char* GetName() const override { return "error"; }
  std::string GetInformation(int c) const override {
    return "[error] " + std::to_string(c);
  }
};

const Error::Category* Cat() {
  static ErrorCategory kC;
  return &kC;
}

CATCH_TEST_CASE("basic", "[result]") {
  {
    Result<int> r;

    CATCH_REQUIRE(!r);
    CATCH_REQUIRE(r.IsEmpty());
    CATCH_REQUIRE(!r.IsError());
    CATCH_REQUIRE(!r.IsOk());
  }

  {
    Result<std::string> r{"123"};

    CATCH_REQUIRE(r);
    CATCH_REQUIRE(r.IsOk());
    CATCH_REQUIRE(!r.IsError());
    CATCH_REQUIRE(!r.IsEmpty());

    CATCH_REQUIRE(r.GetResult() == "123");

    auto result = r.PassResult();
    CATCH_REQUIRE(result == "123");
  }

  {
    Result<bool> r{Error(Cat(), 1, "failed")};

    CATCH_REQUIRE(!r);
    CATCH_REQUIRE(!r.IsOk());
    CATCH_REQUIRE(r.IsError());
    CATCH_REQUIRE(!r.IsEmpty());

    CATCH_REQUIRE(r.GetError().GetMessage() == "failed");

    auto e = r.PassError();
    CATCH_REQUIRE(e.GetMessage() == "failed");
  }

  {
    Result<void> r;

    CATCH_REQUIRE(r);
    CATCH_REQUIRE(!r.IsEmpty());
    CATCH_REQUIRE(r.IsOk());
    CATCH_REQUIRE(!r.IsError());
  }

  {
    Result<void> r{Error(Cat(), 2, "error")};

    CATCH_REQUIRE(!r);
    CATCH_REQUIRE(!r.IsEmpty());
    CATCH_REQUIRE(!r.IsOk());
    CATCH_REQUIRE(r.IsError());

    CATCH_REQUIRE(r.GetError().GetMessage() == "error");

    auto e = r.PassError();
    CATCH_REQUIRE(e.GetMessage() == "error");
  }
}

}  // namespace libz

int main(int argc, char* argv[]) { return Catch::Session().run(argc, argv); }

