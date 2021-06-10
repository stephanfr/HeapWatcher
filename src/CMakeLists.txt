set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(HeapWatcher AllocationRecord.cpp HeapWatcher.cpp)

target_link_libraries(HeapWatcher PRIVATE Threads::Threads)