#include <catch2/catch_all.hpp>
#include <iostream>
#include <thread>

#include "HeapWatcher.hpp"
#include "TestWorkloads.hpp"

//  The pragma below is to disable to false errors flagged by intellisense for
//  Catch2 REQUIRE macros.

#if __INTELLISENSE__
#pragma diag_suppress 2486
#endif

TEST_CASE("Basic HeapWatrcher Tests", "[basic]")
{
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

        REQUIRE(leaks.open_allocations().size() == 0);
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
        REQUIRE_THAT(leaks.open_allocations()[0].stack_trace()[0].function(), Catch::Matchers::Equals("OneLeak()"));
        REQUIRE_THAT(leaks.open_allocations()[0].stack_trace()[1].function(),
                     Catch::Matchers::Equals("OneLeakNested()"));

        REQUIRE(leaks.high_level_statistics().number_of_mallocs() == 1);
        REQUIRE(leaks.high_level_statistics().number_of_frees() == 0);
        REQUIRE(leaks.high_level_statistics().number_of_reallocs() == 0);
        REQUIRE(leaks.high_level_statistics().bytes_allocated() == sizeof(int));
        REQUIRE(leaks.high_level_statistics().bytes_freed() == 0);
    }

    SECTION("No Leaks with Realloc", "[basic]")
    {
        //  Now, one leak that has been reallocated

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        NoLeaksWithRealloc();

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 0);

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

    SECTION("Many Allocations No Leaks", "[basic]")
    {
        //  First, no leaks

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        BuildBigMap();

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 0);
        REQUIRE(leaks.high_level_statistics().number_of_mallocs() == leaks.high_level_statistics().number_of_frees());
        REQUIRE(leaks.high_level_statistics().number_of_reallocs() == 0);
        REQUIRE(leaks.high_level_statistics().bytes_freed() == leaks.high_level_statistics().bytes_allocated());
    }

    SECTION("Many Random Heap Operations No Leaks", "[basic]")
    {
        constexpr long      num_operations = 1000000;

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        RandomHeapOperations(num_operations);

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 0);
    }

    SECTION("Insure getting snapshot does not leak", "[basic]")
    {
        constexpr long      num_operations = 1000000;

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        std::thread heap_loading_thread(RandomHeapOperations, num_operations);

        for (int i = 0; i < 5; i++)
        {
            auto snapshot = SEFUtility::HeapWatcher::get_heap_watcher().snapshot();

            auto heap_stats = SEFUtility::HeapWatcher::get_heap_watcher().high_level_stats();
        }

        heap_loading_thread.join();

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 0);
    }

    SECTION("Multi-threaded stress test", "[basic]")
    {
        constexpr long      num_operations = 1000000;

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        std::thread heap_loading_threads[5];

        for (int i = 0; i < 5; i++)
        {
            heap_loading_threads[i] = std::thread(RandomHeapOperations, num_operations);
        }

        for (int i = 0; i < 5; i++)
        {
            heap_loading_threads[i].join();
        }

        auto leaks(SEFUtility::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 0);
    }
}
