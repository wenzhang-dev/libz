set(EVENT_SRC_PREFIX ${CMAKE_SOURCE_DIR}/event)

set(EVENT_SRC
  ${EVENT_SRC_PREFIX}/basic.cc
  ${EVENT_SRC_PREFIX}/message-loop.cc
  ${EVENT_SRC_PREFIX}/timer-event.cc
)

if(BUILD_TESTS)
  set(ld_libs event base fmt)

  add_tc(NAME "${EVENT_SRC_PREFIX}/promise-test.cc" LIBS ${ld_libs})

if (ENABLE_CO)
  add_tc(NAME "${EVENT_SRC_PREFIX}/coroutine-test.cc" LIBS ${ld_libs})
endif(ENABLE_CO)

endif(BUILD_TESTS)

