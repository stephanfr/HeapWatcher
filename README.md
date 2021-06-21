# HeapWatcher

This project provides a simple tool for tracking heap allocations between start/finish points in C++ code.  It is intended for use in unit test and perhaps some feature tests.  It is not a replacement for Valgrind or other memory debugging tools - the primary intent is to provide an easy-to-use tool that can be dded to unit tests built with GoogleTest or Catch2 and to find leaks and provide partial or full stack dumps of leaked allocations.

# Design

The C standard library functions of _malloc()_, _realloc()_ and _free()_ can be replaced by user-supplied replacements with the same names and signatures supplied in a user static library or shared object.  This tool wraps the c standard library calls and then tracks all allocations and frees in a map.  The 'bookkeeping' is performed in a separate thread to (1) limit the need for mutexes or critical sections to protect shared state and (2) limit the run-time performance impact on the code under test.  Allocation tracking can be paused in any thread being tracked.

There exists a single global static instance of HeapWatcher which can be accessed with the _SEFUtility::HeapWatcher::get_heap_watcher()_ function.

Additionally, there are a pair of multi-threaded test fixtures provided in the project.  One fixture launches workload threads and requires the user to manage the heap watcher.  The second test fixture integrates the heap watcher and tracks all allocations made while the instance of the fixture itself is in scope.

For memory intensive applications running on many cores, the single tracker thread may be insufficient.  All allocation records go into a queue, will not be lost and will eventually be processed.  Potential problems can arise if the application allocates faster than the single thread can keep up and the queue used for passing the records to the tracker thread grows to the point that it exhausts system memory.  When the HeapWatcher stops, the memory snapshot it returns is the result of processing all allocation records - so it should be correct.

# Including into a Project

Probably the easiest way to use HeapWatcher is to include it through the fetch mechanism provided by CMake:

````cmake
    FetchContent_Declare(
        heapwatcher
        GIT_REPOSITORY "https://github.com/stephanfr/HeapWatcher.git" )

    FetchContent_MakeAvailable(heapwatcher)

    include_directories(
        ${heapwatcher_SOURCE_DIR}/include
        ${heapwatcher_BIN_DIR}
    )
````

The CMake specification for HeapWatcher will build the library which muct be linked into your peoject.  In addition, for the call stack decoding to work properly, the following linker option must be included in your project as well:

````cmake
SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")
````

HeapWatcher is not a header-only project, the linker must have concrete instances of _malloc()_, _realloc()_ and _free()_ to link to the rest of the code under test.  Given the ease of including the library with CMake, this doesn't present much of a problem overall.

# Using HeapWatcher

Only a single header file _HeapWatcher.hpp_ must be included in any file wishing to use the tool.  This header contains all the data structures and classes needed to use the tool.  The HeapWatcher class itself is fairly simple and the call to retreive the global instance is trivial :

````cpp
namespace SEFUtility::HeapWatcher
{
    class HeapWatcher
    {
       public:
        virtual void start_watching() = 0;
        virtual HeapSnapshot stop_watching() = 0;

        virtual uint64_t capture_known_leak(std::list<std::string>& leaking_symbols, std::function<void()> function_which_leaks) = 0;
        [[nodiscard]] virtual const KnownLeaks known_leaks() const = 0;

        [[nodiscard]] virtual PauseThreadWatchGuard pause_watching_this_thread() = 0;

        [[nodiscard]] virtual const HeapSnapshot snapshot() = 0;
        [[nodiscard]] virtual const HighLevelStatistics high_level_stats() = 0;
    };
    
    HeapWatcher& get_heap_watcher();
}
````

Note the namesapce declaration.  There are a number of other classes declared in the _HeapWatcher.cpp_ header for the HeapSnapshot and to provide the pause watching capability.  A simple example of using HeapWatcher in a Catch2 test appears below:

````cpp
void OneLeak() { int* new_int = static_cast<int*>(malloc(sizeof(int))); }

void OneLeakNested() { OneLeak(); }

TEST_CASE("Basic HeapWatcher Tests", "[basic]")
{
    SECTION("One Leak Nested", "[basic]")
    {
        //  Now, one leak from a nested function

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        OneLeakNested();

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 1);

        REQUIRE_THAT(leaks.open_allocations()[0].stack_trace()[0].function(), Catch::Matchers::Equals("OneLeak()"));
        REQUIRE_THAT(leaks.open_allocations()[0].stack_trace()[1].function(),
                     Catch::Matchers::Equals("OneLeakNested()"));

        REQUIRE(leaks.high_level_statistics().number_of_mallocs() == 1);
        REQUIRE(leaks.high_level_statistics().number_of_frees() == 0);
        REQUIRE(leaks.high_level_statistics().number_of_reallocs() == 0);
        REQUIRE(leaks.high_level_statistics().bytes_allocated() == sizeof(int));
        REQUIRE(leaks.high_level_statistics().bytes_freed() == 0);
    }
}
````

# Capturing Known Leaks

In various third party libraries there exist intentional leaks.  A good example is the leak of a pointer for thread local storage for each thread created by the pthread library.  There is a leak from the symbol _dl_allocate_tls_ that appears to remain even after std::thread::join() is called.  This appears not infrequently in Valgrind reports as well.  Given the desire to make this a library for automated testing, I added the capability to capture and then ignore allocations from certain functions or methods.  An example appears below:

````
    SECTION("Known Leak", "[basic]")
    {
        std::list<std::string> leaking_symbol({"KnownLeak()"});

        REQUIRE( SEFUtility::HeapWatcher::get_heap_watcher().capture_known_leak(leaking_symbol, []() { KnownLeak(); }) == 1 );

        REQUIRE(SEFUtility::HeapWatcher::get_heap_watcher().known_leaks().addresses().size() == 2);
        REQUIRE_THAT(SEFUtility::HeapWatcher::get_heap_watcher().known_leaks().symbols()[0].function(),
                     Catch::Matchers::Equals("_dl_allocate_tls"));
        REQUIRE_THAT(SEFUtility::HeapWatcher::get_heap_watcher().known_leaks().symbols()[1].function(),
                     Catch::Matchers::Equals("KnownLeak()"));

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        OneLeakNested();
        KnownLeak();
        OneLeak();

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 2);
    }
````

The capture_known_leak() method takes two arguments: 1) a std::list<std::string> containing one or more symbols which if located in a stack trace will cause the allocation associated with the trace to be ignored and 2) a function (or lambda) which will evoke one or more leaks associated with the symbols passed in the first argument.  The leaking function need not be just adjacent to the malloc, it may be further up the call stack but the allocation will only be ignored if it appears at the same number of frames above the memory allocation as at the time the leak was captured.

This approach of actively capturing the leak at runtime is effective for dealing with ASLR (Address Space Layout Randomization) and does not require loading of shared libraries or other linking or loading gymnastics.


# Pausing Allocation Tracking

The _PauseThreadWatchGuard_ instance returned by a call to _HeapWatcher::pause_watching_this_thread()_ is a scope based mechanism for suspending heap activity tracking in a thread.  For example, the above snippet can be modified to not log the leak in _OneLeakNested()_ by obtaining a guard an putting the leaking call into the same scope as the guard:

````cpp
        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        {
          auto pause_watching = SEFUtility::HeapWatcher::get_heap_watcher().pause_watching_this_thread();
          
          OneLeakNested();
        }

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 0);
````

# Test Fixtures

Two test fixtures are included with HeapWatcher and both are intended to ease the creation of multi-threaded unit test cases, which are useful for detecting race conditions or dead locks. The test fixtures feature the ability to add functions or lambdas for ‘workload functions’ and then start all of those ‘workload functions’ simultaneously. Alternatively, ‘workload functions’ may be given a random start delay in seconds (as a double so it may be fractions of a second as well). This permits stress testing with a lot of load started at one time or allows for load to ramp over time.

The SEFUtility::HeapWatcher::ScopedMultithreadedTestFixture class starts watching the heap on creation and takes a function or lambda which will be called with a HeapSnapshot when all threads have completed, to permit testing the final heap state. This test fixture effectively hides the HeapWatcher instructions whereas the SEFUtility::HeapWatcher::MultithreadedTestFixture class requires the user to wrap the test fixture with the HeapWatcher start and stop.

Examples of both test fixtures appear below. First is an example of MultithreadedTestFixture :

````
SECTION("Torture Test, One Leak", "[basic]")
{
    constexpr int64_t num_operations = 2000000;
    constexpr int NUM_WORKERS = 20;
 
    SEFUtility::HeapWatcher::MultithreadedTestFixture test_fixture;
 
    SEFUtility::HeapWatcher::get_heap_watcher().start_watching();
 
    test_fixture.add_workload(NUM_WORKERS,
                              std::bind(&RandomHeapOperations, num_operations));  //  NOLINT(modernize-avoid-bind)
    test_fixture.add_workload(1, &OneLeak);
 
    std::this_thread::sleep_for(10s);
 
    test_fixture.start_workload();
    test_fixture.wait_for_completion();
 
    auto leaks = SEFUtility::HeapWatcher::get_heap_watcher().stop_watching();
 
    REQUIRE(leaks.open_allocations().size() == 1);
}
````

An example of ScopedMultiThreadedTestFixture follows :

````
SECTION("Two Workloads, Few Threads, one Leak", "[basic]")
{
    constexpr int NUM_WORKERS = 5;
 
    SEFUtility::HeapWatcher::ScopedMultithreadedTestFixture test_fixture(
        [](const SEFUtility::HeapWatcher::HeapSnapshot& snapshot) { REQUIRE(snapshot.numberof_leaks() == 5); });
 
    test_fixture.add_workload(NUM_WORKERS, &BuildBigMap);
    test_fixture.add_workload(NUM_WORKERS, &OneLeak);
 
    std::this_thread::sleep_for(1s);
 
    test_fixture.start_workload();
}
````

# Conclusion

HeapWatcher and the multithreaded test fixture classes are intended to help developers create tests which check for memory leaks either in simple procedural test cases written with GoogleTest or Catch2 or in more complex multi-threaded tests in those same base frameworks.

