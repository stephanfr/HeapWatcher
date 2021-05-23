#include <catch2/catch_all.hpp>
#include <deque>
#include <iostream>
#include <thread>

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
    std::default_random_engine e1(r());
    std::uniform_int_distribution<uint64_t> uniform_dist(1, 10000000000);

    std::map<uint64_t, uint64_t> big_map;

    for (long i = 0; i < 1000000; i++)
    {
        big_map.insert_or_assign(uniform_dist(e1), uniform_dist(e1));
    }
}

void RandomHeapOperations()
{
    constexpr long NUM_OPERATIONS = 10000000;
    constexpr long INITIAL_DEQUEUE_SIZE = 10000;
    constexpr int MAX_ALLOCATION_SIZE = 1024;
    constexpr int MIN_ALLOCATION_SIZE = 2;

    std::deque<void*> heap_blocks;

    std::random_device r;
    std::default_random_engine e1(r());

    std::uniform_int_distribution<int> operation_dist(0, 3);
    std::uniform_int_distribution<int> allocation_size_dist(MIN_ALLOCATION_SIZE, MAX_ALLOCATION_SIZE);

    long mallocs = 0;
    long reallocs = 0;
    long frees = 0;

    for (int i = 0; i < NUM_OPERATIONS; i++)
    {
        switch (operation_dist(e1))
        {
            case 0:
            case 1:
            {
                heap_blocks.emplace_back(malloc(allocation_size_dist(e1)));
                mallocs++;
            }
            break;

            case 2:
            {
                if (heap_blocks.size() > 10)
                {
                    long allocation_index = std::uniform_int_distribution<long>(0, heap_blocks.size() - 1)(e1);

                    heap_blocks[allocation_index] = realloc(heap_blocks[allocation_index], allocation_size_dist(e1));
                    reallocs++;
                }
            }

            case 3:
            {
                if (heap_blocks.size() > 10)
                {
                    long allocation_index = std::uniform_int_distribution<long>(0, heap_blocks.size() - 1)(e1);

                    free(heap_blocks[allocation_index]);
                    frees++;

                    if (allocation_index != heap_blocks.size() - 1)
                    {
                        heap_blocks[allocation_index] = heap_blocks.back();
                    }
                    heap_blocks.pop_back();
                }
            }
            break;
        }
    }

    SEFUtils::HeapWatcher::HighLevelStatistics stats(SEFUtils::HeapWatcher::get_heap_watcher().high_level_stats());

    //  Delete all the blocks

    for (auto block : heap_blocks)
    {
        free(block);
    }

    //  After exiting this function, all heap activity should be complete and there should be zero leaks.
}

TEST_CASE("Basic HeapWatrcher Tests", "[basic]")
{
    SECTION("No Leaks", "[basic]")
    {
        //  First, no leaks

        SEFUtils::HeapWatcher::get_heap_watcher().start_watching();

        {
            auto stats = SEFUtils::HeapWatcher::get_heap_watcher().high_level_stats();

            REQUIRE(stats.number_of_mallocs() == 0);
            REQUIRE(stats.number_of_reallocs() == 0);
            REQUIRE(stats.number_of_frees() == 0);

            REQUIRE(stats.bytes_allocated() == 0);
            REQUIRE(stats.bytes_freed() == 0);
        }

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

    SECTION("Many Allocations No Leaks", "[basic]")
    {
        //  First, no leaks

        SEFUtils::HeapWatcher::get_heap_watcher().start_watching();

        BuildBigMap();

        auto leaks(SEFUtils::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 0);
        REQUIRE(leaks.high_level_statistics().number_of_mallocs() == leaks.high_level_statistics().number_of_frees());
        REQUIRE(leaks.high_level_statistics().number_of_reallocs() == 0);
        REQUIRE(leaks.high_level_statistics().bytes_freed() == leaks.high_level_statistics().bytes_allocated());
    }

    SECTION("Many Random Heap Operations No Leaks", "[basic]")
    {
        SEFUtils::HeapWatcher::get_heap_watcher().start_watching();

        RandomHeapOperations();

        auto leaks(SEFUtils::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 0);
    }

    SECTION("Insure getting snapshot does not leak", "[basic]")
    {
        SEFUtils::HeapWatcher::get_heap_watcher().start_watching();

        std::thread heap_loading_thread(RandomHeapOperations);

        for (int i = 0; i < 5; i++)
        {
            auto snapshot = SEFUtils::HeapWatcher::get_heap_watcher().snapshot();

            auto heap_stats = SEFUtils::HeapWatcher::get_heap_watcher().high_level_stats();
        }

        heap_loading_thread.join();

        auto leaks(SEFUtils::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 0);
    }

    SECTION("Multi-threaded stress test", "[basic]")
    {
        SEFUtils::HeapWatcher::get_heap_watcher().start_watching();

        std::thread heap_loading_threads[5];

        for (int i = 0; i < 5; i++)
        {
            heap_loading_threads[i] = std::thread(RandomHeapOperations);
        }

        for (int i = 0; i < 5; i++)
        {
            heap_loading_threads[i].join();
        }

        auto leaks(SEFUtils::HeapWatcher::get_heap_watcher().stop_watching());

        REQUIRE(leaks.open_allocations().size() == 0);
    }
}
