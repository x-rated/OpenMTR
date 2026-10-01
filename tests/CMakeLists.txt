# Engine tests: tracer.cpp on its own — no Qt, no window, and nothing is sent
# off the machine. Configure with -DOPENMTR_BUILD_TESTS=ON, then
#
#   cmake --build build --target engine_tests
#   ctest --test-dir build --output-on-failure
#
# CI runs them on macOS. They have also been built and run on Linux (GCC 14,
# in a container), but never on Windows.
find_package(Threads REQUIRED)

add_executable(engine_tests engine_tests.cpp ${PROJECT_SOURCE_DIR}/src/tracer.cpp)
# The project turns AUTOMOC on globally; nothing here is a Qt class.
set_target_properties(engine_tests PROPERTIES AUTOMOC OFF)
target_include_directories(engine_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(engine_tests PRIVATE Threads::Threads)
if(NOT MSVC)
    target_compile_options(engine_tests PRIVATE -Wall -Wextra)
endif()

foreach(name IN ITEMS dns_lifetime status_text v6_silent_first_hop
                      stop_before_start stop_right_after_start)
    add_test(NAME engine.${name} COMMAND engine_tests ${name})
    # A regression here can hang rather than crash, so bound every case.
    # 77 = this machine cannot run the case (e.g. no IPv6 ICMP socket):
    # reported as skipped, never as passed.
    set_tests_properties(engine.${name} PROPERTIES TIMEOUT 20 SKIP_RETURN_CODE 77)
endforeach()
