cmake_minimum_required(VERSION 3.12)

project(libz VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_CO "enable coreoutine" ON)

if (ENABLE_CO)
  set(CMAKE_CXX_STANDARD 20)
  add_compile_definitions(ENABLE_CO)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines")
else()
  set(CMAKE_CXX_STANDARD 17)
endif()

set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -fPIC -fPIE -Wall -rdynamic")

include_directories(
  ${CMAKE_CURRENT_LIST_DIR}
  ${PROJECT_SOURCE_DIR}/third-party
  ${PROJECT_SOURCE_DIR}/third-party/fmt/include
  ${PROJECT_SOURCE_DIR}/third-party/asio/asio/include
)

# fmt lib
add_subdirectory("third-party/fmt")

set(CMAKE_BUILD_TYPE None)
option(BUILD_TESTS "build unit tests" ON)
option(RELEASE_BUILD "build type: release or not" OFF)

if (RELEASE_BUILD)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -g3 -DNDEBUG=1")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g3")
endif()

# unit test
if (BUILD_TESTS)
  enable_testing()

  add_subdirectory("third-party/catch2")
  include(CTest)
  include(Catch)

  add_compile_options(-fsanitize=address)
  add_link_options(-fsanitize=address)

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DLIBZ_DEBUG=1")

  function(add_tc)
    cmake_parse_arguments(
      TC "" "NAME" "LIBS"
      ${ARGN}
    )

    get_filename_component(test_target_name "${TC_NAME}" NAME_WE)

    add_executable("${test_target_name}" "")
    target_sources("${test_target_name}" PRIVATE "${TC_NAME}")
    
    target_link_libraries("${test_target_name}" ${TC_LIBS} Catch2::Catch2 dl pthread)

    add_test(NAME "${test_target_name}" COMMAND "${test_target_name}")
  endfunction()
endif()

# base lib
include(${CMAKE_SOURCE_DIR}/base/sources.cmake)
add_library(base STATIC ${BASE_SRC})

# event lib
include(${CMAKE_SOURCE_DIR}/event/sources.cmake)
add_library(event STATIC ${EVENT_SRC})

# examples
option(BUILD_EXAMPLES "build examples" OFF)
option(ENABLE_IOURING "enable iouring" OFF)
if (BUILD_EXAMPLES)
  add_executable(echo_tcp_server examples/echo_tcp_server.cc)
  target_link_libraries(echo_tcp_server event base fmt pthread)

  if (ENABLE_IOURING)
    add_executable(disk_io
      examples/disk_io.cc
    )

    target_link_directories(disk_io PUBLIC "${IOURING_PATH}/lib")
    target_include_directories(disk_io PUBLIC "${IOURING_PATH}/include")

    target_link_libraries(disk_io
      event
      base
      fmt
      uring
      pthread
    )
  endif()

  #  set(CMAKE_PREFIX_PATH "/the/path/of/grpc")
  find_package(Protobuf REQUIRED CONFIG)
  find_package(gRPC REQUIRED)
  message(STATUS "using grpc ${gRPC_VERSION}")

  add_executable(echo_grpc_server
    examples/echo_grpc_server.cc
    examples/helloworld.grpc.pb.cc
    examples/helloworld.grpc.pb.h
    examples/helloworld.pb.cc
    examples/helloworld.pb.h
  )

  # don't use asan for grpc
  # turn on asan, the native grpc examples won't run.
  target_compile_options(echo_grpc_server PUBLIC -fno-sanitize=all)

  target_link_libraries(echo_grpc_server
    gRPC::grpc++_reflection
    gRPC::grpc++
    protobuf::libprotobuf
    event
    base
    fmt
    pthread
  )

  add_executable(echo_grpc_server2
    examples/echo_grpc_server2.cc
    examples/helloworld2.grpc.pb.cc
    examples/helloworld2.grpc.pb.h
    examples/helloworld2.pb.cc
    examples/helloworld2.pb.h
  )

  target_compile_options(echo_grpc_server2 PUBLIC -fno-sanitize=all)
  target_link_libraries(echo_grpc_server2
    gRPC::grpc++_reflection
    gRPC::grpc++
    protobuf::libprotobuf
    event
    base
    fmt
    pthread
  )

  add_executable(echo_grpc_client
    examples/echo_grpc_client.cc
    examples/helloworld2.grpc.pb.cc
    examples/helloworld2.grpc.pb.h
    examples/helloworld2.pb.cc
    examples/helloworld2.pb.h
  )

  target_compile_options(echo_grpc_client PUBLIC -fno-sanitize=all)
  target_link_libraries(echo_grpc_client
    gRPC::grpc++_reflection
    gRPC::grpc++
    protobuf::libprotobuf
    event
    base
    fmt
    pthread
  )

endif()
