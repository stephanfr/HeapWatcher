#include <catch2/catch_all.hpp>
#include <iostream>

#include "HeapWatcher.hpp"

//  The pragma below is to disable to false errors flagged by intellisense for
//  Catch2 REQUIRE macros.

#if __INTELLISENSE__
#pragma diag_suppress 2486
#endif

void NoLeaks()
{
    int* new_int = static_cast<int*>(malloc(sizeof(int)));

    free(new_int);
}

void OneLeak() { int* new_int = static_cast<int*>(malloc(sizeof(int))); }

void OneLeakNested() { OneLeak(); }

void NoLeaksWithRealloc()
{
    int* new_int = static_cast<int*>(malloc(sizeof(int)));

    new_int = static_cast<int*>(realloc(new_int, sizeof(int) * 2));

    free(new_int);
}

void OneLeakWithRealloc()
{
    int* new_int = static_cast<int*>(malloc(sizeof(int)));
    new_int = static_cast<int*>(realloc(new_int, sizeof(int) * 2));
}


void BuildBigMap()
{
    std::random_device r;

    // Choose a random mean between 1 and 6
    std::default_random_engine e1(r());
    std::uniform_int_distribution<uint64_t> uniform_dist(1, 10000000000);

    std::map<uint64_t, uint64_t> big_map;

    for (long i = 0; i < 1000000; i++)
    {
        big_map.insert_or_assign(uniform_dist(e1), uniform_dist(e1));
    }
}


TEST_CASE("Basic EpollEventManager Tests", "[basic]")
{
    SECTION("No Leaks", "[basic]")
    {
        //  First, no leaks

        SEFUtils::HeapWatcher::get_heap_watcher().start_watching();

        NoLeaks();

        auto leaks(SEFUtils::HeapWatcher::get_heap_watcher().stop_watching());

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

        SEFUtils::HeapWatcher::get_heap_watcher().start_watching();

        OneLeak();

        auto leaks(SEFUtils::HeapWatcher::get_heap_watcher().stop_watching());

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

        SEFUtils::HeapWatcher::get_heap_watcher().start_watching();

        OneLeakNested();

        auto leaks(SEFUtils::HeapWatcher::get_heap_watcher().stop_watching());

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

        SEFUtils::HeapWatcher::get_heap_watcher().start_watching();

        NoLeaksWithRealloc();

        auto leaks(SEFUtils::HeapWatcher::get_heap_watcher().stop_watching());

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

        SEFUtils::HeapWatcher::get_heap_watcher().start_watching();

        OneLeakWithRealloc();

        auto leaks(SEFUtils::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 1);
        REQUIRE_THAT(leaks.open_allocations()[0].stack_trace()[0].function(),
                     Catch::Matchers::Equals("OneLeakWithRealloc()"));

        REQUIRE(leaks.high_level_statistics().number_of_mallocs() == 1);
        REQUIRE(leaks.high_level_statistics().number_of_frees() == 0);
        REQUIRE(leaks.high_level_statistics().number_of_reallocs() == 1);
        REQUIRE(leaks.high_level_statistics().bytes_allocated() == sizeof(int) * 2);
        REQUIRE(leaks.high_level_statistics().bytes_freed() == 0);
    }

}
