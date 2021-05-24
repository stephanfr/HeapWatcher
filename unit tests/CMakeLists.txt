
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
)

FetchContent_MakeAvailable(googletest)


include(GoogleTest)


find_package(Catch2 REQUIRED)
add_executable(tests BasicTests.cpp TestFixtureTests.cpp TestWorkloads.cpp)
target_include_directories( tests PRIVATE ../include ../src )
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain HeapWatcher )

include(CTest)
include(Catch)
catch_discover_tests(tests)
