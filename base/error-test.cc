#define CATCH_CONFIG_PREFIX_ALL
#include "error.h"

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

CATCH_TEST_CASE("basic", "[error]") {
  {
    Error e;

    CATCH_REQUIRE(!e);
    CATCH_REQUIRE(!e.Has());
    CATCH_REQUIRE(e.code() == 0);
    CATCH_REQUIRE(e.category() == nullptr);
  }

  {
    Error e{Cat(), 1, "failed"};

    CATCH_REQUIRE(e);
    CATCH_REQUIRE(e.Has());
    CATCH_REQUIRE(e.code() == 1);
    CATCH_REQUIRE(e.category() == Cat());
    CATCH_REQUIRE(e.GetMessage() == "failed");
    CATCH_REQUIRE(e.information() == "[error] 1");
  }

  {
    Error e{Cat(), 2, "err"};

    auto e1 = e;
    CATCH_REQUIRE(e1);
    CATCH_REQUIRE(e1.Has());
    CATCH_REQUIRE(e1.code() == 2);
    CATCH_REQUIRE(e1.category() == Cat());
    CATCH_REQUIRE(e1.GetMessage() == "err");
    CATCH_REQUIRE(e1.information() == "[error] 2");
  }

  {
    Error e{Cat(), 3, "err"};

    auto e1 = std::move(e);
    CATCH_REQUIRE(e1);
    CATCH_REQUIRE(e1.Has());
    CATCH_REQUIRE(e1.code() == 3);
    CATCH_REQUIRE(e1.category() == Cat());
    CATCH_REQUIRE(e1.GetMessage() == "err");
    CATCH_REQUIRE(e1.information() == "[error] 3");
    CATCH_REQUIRE(e1.Details() == "[error] 3: err");
  }

  {
    // no error message
    Error e1{Cat(), 4};

    CATCH_REQUIRE(e1);
    CATCH_REQUIRE(e1.Has());
    CATCH_REQUIRE(e1.code() == 4);
    CATCH_REQUIRE(e1.category() == Cat());
    CATCH_REQUIRE(!e1.HasMessage());
    CATCH_REQUIRE(e1.information() == "[error] 4");
    CATCH_REQUIRE(e1.Details() == "[error] 4: ");
  }

  {
    // errno
    auto e = Error::MkSysError(1);

    CATCH_REQUIRE(e);
    CATCH_REQUIRE(e.IsSysError());
    CATCH_REQUIRE(e.code() == 1);
    CATCH_REQUIRE(!e.HasMessage());
  }

  {
    // boost ec
    auto e = Error::MkBoostError(1, "err");

    CATCH_REQUIRE(e);
    CATCH_REQUIRE(e.IsBoostError());
    CATCH_REQUIRE(e.code() == 1);
    CATCH_REQUIRE(e.GetMessage() == "err");
  }
}

}  // namespace libz

int main(int argc, char* argv[]) { return Catch::Session().run(argc, argv); }
