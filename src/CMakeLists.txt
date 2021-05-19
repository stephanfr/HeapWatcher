add_library(HeapWatcher AllocationRecord.cpp HeapWatcher.cpp)

target_link_libraries(HeapWatcher PRIVATE pthread)