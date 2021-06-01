#include <catch2/catch_all.hpp>
#include <chrono>
#include <iostream>

#include "MultithreadedTestFixture.hpp"
#include "TestWorkloads.hpp"

using namespace std::literals::chrono_literals;

TEST_CASE("Basic MultithreadedTestFixture Tests", "[basic]")
{
    SECTION("One Workload, Few Threads, No Leaks", "[basic]")
    {
        constexpr int NUM_WORKERS = 5;

        SEFUtility::HeapWatcher::MultithreadedTestFixture test_fixture;

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        test_fixture.add_workload(NUM_WORKERS, &BuildBigMap, 1);

        std::this_thread::sleep_for(1s);

        test_fixture.start_workload();
        test_fixture.wait_for_completion();

        auto leaks = SEFUtility::HeapWatcher::get_heap_watcher().stop_watching();

        REQUIRE(!leaks.has_leaks());
    }

    SECTION("Two Workloads, Few Threads, one Leak", "[basic]")
    {
        constexpr int NUM_WORKERS = 5;

        SEFUtility::HeapWatcher::MultithreadedTestFixture test_fixture;

        SEFUtility::HeapWatcher::get_heap_watcher().start_watching();

        test_fixture.add_workload(NUM_WORKERS, &BuildBigMap);
        test_fixture.add_workload(NUM_WORKERS, &OneLeak);

        std::this_thread::sleep_for(1s);

        test_fixture.start_workload();
        test_fixture.wait_for_completion();

        auto leaks = SEFUtility::HeapWatcher::get_heap_watcher().stop_watching();

        REQUIRE(leaks.open_allocations().size() == NUM_WORKERS);
    }

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

    SECTION("Scoped Fixture, One Workload, Few Threads, No Leaks", "[basic]")
    {
        constexpr int NUM_WORKERS = 5;

        SEFUtility::HeapWatcher::ScopedMultithreadedTestFixture test_fixture(
            [](const SEFUtility::HeapWatcher::HeapSnapshot& snapshot) { REQUIRE(!snapshot.has_leaks()); });

        test_fixture.add_workload(NUM_WORKERS, &BuildBigMap, 1);

        std::this_thread::sleep_for(1s);

        test_fixture.start_workload();
    }

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
}
