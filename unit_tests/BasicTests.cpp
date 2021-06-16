#include <catch2/catch_all.hpp>
#include <future>
#include <iostream>
#include <thread>

#include "HeapWatcher.hpp"
#include "TestWorkloads.hpp"

using namespace std::literals::chrono_literals;

extern "C" void* _dl_allocate_tls(void*);

//  The pragma below is to disable to false errors flagged by intellisense for
//  Catch2 REQUIRE macros.

#if __INTELLISENSE__
#pragma diag_suppress 2486
#endif

TEST_CASE("Basic HeapWatcher Tests", "[basic]")
{
    SECTION("Two Known Leaks", "[basic]")
    {
        //  Initially we should have only a single know leak that is populated by the
        //      constructor.  This is a purposeful leak of pthread thread local storage.

        REQUIRE(SEFUtility::HeapWatcher::get_heap_watcher().known_leaks().addresses().size() == 1);
        REQUIRE_THAT(SEFUtility::HeapWatcher::get_heap_watcher().known_leaks().symbols()[0].function(),
                     Catch::Matchers::Equals("_dl_allocate_tls"));
    }

    SECTION("No Leaks", "[basic]")
    {
        //  First, no leaks

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        {
            auto stats = SEFUtility::HeapWatcher::get_heap_watcher().high_level_stats();

            REQUIRE(stats.number_of_mallocs() == 0);
            REQUIRE(stats.number_of_reallocs() == 0);
            REQUIRE(stats.number_of_frees() == 0);

            REQUIRE(stats.bytes_allocated() == 0);
            REQUIRE(stats.bytes_freed() == 0);
        }

        NoLeaks();

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 0);  //  NOLINT
        REQUIRE(!leaks.has_leaks());
        REQUIRE(leaks.high_level_statistics().number_of_mallocs() == 1);
        REQUIRE(leaks.high_level_statistics().number_of_frees() == 1);
        REQUIRE(leaks.high_level_statistics().number_of_reallocs() == 0);
        REQUIRE(leaks.high_level_statistics().bytes_allocated() == sizeof(int));
        REQUIRE(leaks.high_level_statistics().bytes_freed() == sizeof(int));
    }

    SECTION("One Leak", "[basic]")
    {
        //  Now, one leak

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        OneLeak();

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 1);

        REQUIRE_THAT(leaks.open_allocations()[0].stack_trace()[0].function(), Catch::Matchers::Equals("OneLeak()"));

        REQUIRE(leaks.high_level_statistics().number_of_mallocs() == 1);
        REQUIRE(leaks.high_level_statistics().number_of_frees() == 0);
        REQUIRE(leaks.high_level_statistics().number_of_reallocs() == 0);
        REQUIRE(leaks.high_level_statistics().bytes_allocated() == sizeof(int));
        REQUIRE(leaks.high_level_statistics().bytes_freed() == 0);
    }

    SECTION("One Leak Nested", "[basic]")
    {
        //  Now, one leak from a nested function

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        OneLeakNested();

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 1);

        int i = 0;
        for (auto leak : leaks.open_allocations()[0].stack_trace())
        {
            char buffer[32];
            std::sprintf(buffer, "%p", leaks.open_allocations()[0].raw_stack_trace()[i++]);
            REQUIRE_THAT(buffer, Catch::Matchers::Equals(leak.address()));
        }

        REQUIRE_THAT(leaks.open_allocations()[0].stack_trace()[0].function(), Catch::Matchers::Equals("OneLeak()"));
        REQUIRE_THAT(leaks.open_allocations()[0].stack_trace()[1].function(),
                     Catch::Matchers::Equals("OneLeakNested()"));

        REQUIRE(leaks.high_level_statistics().number_of_mallocs() == 1);
        REQUIRE(leaks.high_level_statistics().number_of_frees() == 0);
        REQUIRE(leaks.high_level_statistics().number_of_reallocs() == 0);
        REQUIRE(leaks.high_level_statistics().bytes_allocated() == sizeof(int));
        REQUIRE(leaks.high_level_statistics().bytes_freed() == 0);
    }

    SECTION("One Calloc Leak", "[basic]")
    {
        //  Now, one leak

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        OneCallocLeak();

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 1);

        REQUIRE_THAT(leaks.open_allocations()[0].stack_trace()[0].function(),
                     Catch::Matchers::Equals("OneCallocLeak()"));

        REQUIRE(leaks.high_level_statistics().number_of_mallocs() == 1);
        REQUIRE(leaks.high_level_statistics().number_of_frees() == 0);
        REQUIRE(leaks.high_level_statistics().number_of_reallocs() == 0);
        REQUIRE(leaks.high_level_statistics().bytes_allocated() == sizeof(long) * NUMBER_OF_LONGS_IN_ONE_CALLOC_LEAK);
        REQUIRE(leaks.high_level_statistics().bytes_freed() == 0);
    }

    SECTION("No Leaks with Realloc", "[basic]")
    {
        //  Now, one leak that has been reallocated

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        NoLeaksWithRealloc();

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(!leaks.has_leaks());

        REQUIRE(leaks.high_level_statistics().number_of_mallocs() == 1);
        REQUIRE(leaks.high_level_statistics().number_of_frees() == 1);
        REQUIRE(leaks.high_level_statistics().number_of_reallocs() == 1);
        REQUIRE(leaks.high_level_statistics().bytes_allocated() == sizeof(int) * 2);
        REQUIRE(leaks.high_level_statistics().bytes_freed() == sizeof(int) * 2);
    }

    SECTION("One Leak with Realloc", "[basic]")
    {
        //  Now, one leak that has been reallocated

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        OneLeakWithRealloc();

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 1);

        REQUIRE_THAT(leaks.open_allocations()[0].stack_trace()[0].function(),
                     Catch::Matchers::Equals("OneLeakWithRealloc()"));

        REQUIRE(leaks.high_level_statistics().number_of_mallocs() == 1);
        REQUIRE(leaks.high_level_statistics().number_of_frees() == 0);
        REQUIRE(leaks.high_level_statistics().number_of_reallocs() == 1);
        REQUIRE(leaks.high_level_statistics().bytes_allocated() == sizeof(int) * 2);
        REQUIRE(leaks.high_level_statistics().bytes_freed() == 0);
    }

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

    SECTION("Many Allocations No Leaks", "[basic]")
    {
        //  First, no leaks

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        BuildBigMap();

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(!leaks.has_leaks());
        REQUIRE(leaks.high_level_statistics().number_of_mallocs() == leaks.high_level_statistics().number_of_frees());
        REQUIRE(leaks.high_level_statistics().number_of_reallocs() == 0);
        REQUIRE(leaks.high_level_statistics().bytes_freed() == leaks.high_level_statistics().bytes_allocated());
    }

    SECTION("Many Random Heap Operations No Leaks", "[basic]")
    {
        constexpr int64_t num_operations = 1000000;

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        RandomHeapOperations(num_operations);

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(!leaks.has_leaks());
    }

    SECTION("Insure getting snapshot does not leak", "[basic]")
    {
        constexpr int64_t num_operations = 1000000;
        constexpr int NUM_SNAPSHOTS = 5;

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        //  Scoping is to prevent the heap_loading_thread from leaking
        {
            std::thread heap_loading_thread(RandomHeapOperations, num_operations);

            for (int i = 0; i < NUM_SNAPSHOTS; i++)
            {
                auto snapshot = SEFUtility::HeapWatcher::get_heap_watcher().snapshot();

                auto heap_stats = SEFUtility::HeapWatcher::get_heap_watcher().high_level_stats();  //  NOLINT
            }

            heap_loading_thread.join();
        }

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(!leaks.has_leaks());
    }

    SECTION("Multi-threaded stress test", "[basic]")
    {
        constexpr int64_t NUM_OPERATIONS = 1000000;
        constexpr int NUM_THREADS = 5;

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        //  Scoping is to prevent the heap_loading_threads from leaking
        {
            std::array<std::thread, NUM_THREADS> heap_loading_threads;

            for (int i = 0; i < NUM_THREADS; i++)
            {
                heap_loading_threads.at(i) = std::thread(RandomHeapOperations, NUM_OPERATIONS);
            }

            for (int i = 0; i < NUM_THREADS; i++)
            {
                heap_loading_threads.at(i).join();
            }
        }

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(!leaks.has_leaks());
    }

    SECTION("Multi-threaded out of order free/malloc message test", "[basic]")
    {
        constexpr int64_t NUM_OPERATIONS = 2000;

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        for (int i = 0; i < NUM_OPERATIONS; i++)
        {
            std::thread race_thread(MallocFreeRace);

            race_thread.join();
        }

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(!leaks.has_leaks());
    }

    SECTION("Pause Tracking", "[basic]")
    {
        //  Now, one leak from a nested function

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        {
            auto pause_guard = SEFUtility::HeapWatcher::get_heap_watcher().pause_watching_this_thread();

            OneLeakNested();
        }

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 0);

        REQUIRE(leaks.high_level_statistics().number_of_mallocs() == 0);
        REQUIRE(leaks.high_level_statistics().number_of_frees() == 0);
        REQUIRE(leaks.high_level_statistics().number_of_reallocs() == 0);
        REQUIRE(leaks.high_level_statistics().bytes_allocated() == 0);
        REQUIRE(leaks.high_level_statistics().bytes_freed() == 0);
    }
}
