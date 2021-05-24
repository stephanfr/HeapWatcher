#include <catch2/catch_all.hpp>

#include "MultithreadedTestFixture.hpp"
#include "TestWorkloads.hpp"

#include <chrono>
#include <iostream>

using namespace std::literals::chrono_literals;

TEST_CASE("Basic MultithreadedTestFixture Tests", "[basic]")
{
    SECTION("One Workload, Few Threads, No Leaks", "[basic]")
    {
        SEFUtils::HeapWatcher::MultithreadedTestFixture     test_fixture;

        test_fixture.add_workload( 5, &BuildBigMap, 1 );

        std::this_thread::sleep_for(1s);

        test_fixture.start_workload();
        auto leaks = test_fixture.wait_for_completion();

        REQUIRE( leaks.open_allocations().size() == 0 );
    }
    
    SECTION("Two Workloads, Few Threads, one Leak", "[basic]")
    {
        SEFUtils::HeapWatcher::MultithreadedTestFixture     test_fixture;

        test_fixture.add_workload( 5, &BuildBigMap );
        test_fixture.add_workload( 5, &OneLeak );

        std::this_thread::sleep_for(1s);

        test_fixture.start_workload();
        auto leaks = test_fixture.wait_for_completion();

        REQUIRE( leaks.open_allocations().size() == 5 );
    }

    SECTION("Torture Test, One Leak", "[basic]")
    {
        constexpr long      num_operations = 2000000;

        SEFUtils::HeapWatcher::MultithreadedTestFixture     test_fixture;

        test_fixture.add_workload( 20, std::bind( &RandomHeapOperations, num_operations ) );
        test_fixture.add_workload( 1, &OneLeak );

        std::this_thread::sleep_for(1s);

        test_fixture.start_workload();
        auto leaks = test_fixture.wait_for_completion();

        REQUIRE( leaks.open_allocations().size() == 1 );
    }
}


