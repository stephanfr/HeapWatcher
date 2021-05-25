cmake_minimum_required(VERSION 3.14.0)
project(HeapWatcher VERSION 0.1.0)

# Determine if fmt is built as a subproject (using add_subdirectory)
# or if it is the master project.
if (NOT DEFINED HEAPWATCHER_IS_MASTER_PROJECT)
  set(HEAPWATCHER_IS_MASTER_PROJECT OFF)
  if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(HEAPWATCHER_IS_MASTER_PROJECT ON)
    message(STATUS "CMake version: ${CMAKE_VERSION}")
    message(STATUS "HeapWatcher is master project")
  endif ()
endif ()

# specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
#set(CMAKE_CXX_CLANG_TIDY "clang-tidy;-checks=*,-fuchsia-default-arguments")

#add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")

include(FetchContent)


FetchContent_Declare(
  spdlog
  GIT_REPOSITORY "https://github.com/gabime/spdlog.git"
  GIT_TAG        v1.8.2)

FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
  concurrentqueue
  GIT_REPOSITORY "https://github.com/cameron314/concurrentqueue.git"
  GIT_TAG        v1.0.3 )

FetchContent_MakeAvailable(concurrentqueue)


include_directories(
  ${spdlog_SOURCE_DIR}/include
  ${spdlog_BIN_DIR}
  ${concurrentqueue_SOURCE_DIR}
  )

enable_testing()

include_directories(include)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

add_subdirectory("src")

if( HEAPWATCHER_IS_MASTER_PROJECT )
  add_subdirectory("unit_tests")
endif()

