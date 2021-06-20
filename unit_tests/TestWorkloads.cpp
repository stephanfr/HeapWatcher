#include "TestWorkloads.hpp"

#include <deque>
#include <future>
#include <iostream>
#include <map>
#include <random>

using namespace std::literals::chrono_literals;

#pragma GCC push_options
#pragma GCC optimize("O0")



void NoLeaks()
{
    int* new_int = static_cast<int*>(malloc(sizeof(int)));          //  NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)

    free(new_int);          //  NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)
}

void KnownLeak() { int* new_int = static_cast<int*>(malloc(sizeof(uint64_t))); }   //  NOLINT

void KnownLeak2() { int* new_int = static_cast<int*>(malloc(2 * sizeof(uint64_t))); }   //  NOLINT

void NestedKnownLeak2() { KnownLeak2(); }   //  NOLINT

void OneLeak() { int* new_int = static_cast<int*>(malloc(sizeof(uint32_t))); }   //  NOLINT

void OneLeakNested() { OneLeak(); }

void OneCallocLeak() { long* new_int = static_cast<long*>(calloc(sizeof(long), NUMBER_OF_LONGS_IN_ONE_CALLOC_LEAK)); }   //  NOLINT

void NoLeaksWithRealloc()
{
    int* new_int = static_cast<int*>(malloc(sizeof(int)));          //  NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)

    new_int = static_cast<int*>(realloc(new_int, sizeof(int) * 2));          //  NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)

    free(new_int);          //  NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)
}

void OneLeakWithRealloc()
{
    int* new_int = static_cast<int*>(malloc(sizeof(int)));                        //  NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)
    new_int = static_cast<int*>(realloc(new_int, sizeof(int) * 2));               //  NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc,clang-analyzer-deadcode.DeadStores)
}   //  NOLINT

void BuildBigMap()
{
    constexpr uint64_t      MAX_ELEMENT_VALUE = 10000000000;
    constexpr int64_t       NUM_ITERATIONS = 1000000;

    std::random_device r;
    std::default_random_engine e1(r());
    std::uniform_int_distribution<uint64_t> uniform_dist(1, MAX_ELEMENT_VALUE);

    std::map<uint64_t, uint64_t> big_map;

    for (int64_t i = 0; i < NUM_ITERATIONS; i++)
    {
        big_map.insert_or_assign(uniform_dist(e1), uniform_dist(e1));
    }
}

void RandomHeapOperations(int64_t num_operations)
{
    constexpr int64_t INITIAL_DEQUEUE_SIZE = 10000;
    constexpr int MAX_ALLOCATION_SIZE = 1024;
    constexpr int MIN_ALLOCATION_SIZE = 2;
    constexpr int MIN_BLOCKS_FOR_REALLOC = 10;

    std::deque<void*> heap_blocks;

    std::random_device r;
    std::default_random_engine e1(r());

    std::uniform_int_distribution<int> operation_dist(0, 3);
    std::uniform_int_distribution<int> allocation_size_dist(MIN_ALLOCATION_SIZE, MAX_ALLOCATION_SIZE);

    int64_t mallocs = 0;
    int64_t reallocs = 0;
    int64_t frees = 0;

    for (int64_t i = 0; i < num_operations; i++)
    {
        switch (operation_dist(e1))
        {
            case 0:
            case 1:
            {
                heap_blocks.emplace_back(malloc(allocation_size_dist(e1)));          //  NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)
                mallocs++;
            }
            break;

            case 2:
            {
                if (heap_blocks.size() > MIN_BLOCKS_FOR_REALLOC)
                {
                    int64_t allocation_index = std::uniform_int_distribution<int64_t>(0, heap_blocks.size() - 1)(e1);

                    heap_blocks[allocation_index] = realloc(heap_blocks[allocation_index], allocation_size_dist(e1));          //  NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)
                    reallocs++;
                }
            }

            case 3:
            {
                if (heap_blocks.size() > MIN_BLOCKS_FOR_REALLOC)
                {
                    int64_t allocation_index = std::uniform_int_distribution<int64_t>(0, heap_blocks.size() - 1)(e1);

                    free(heap_blocks[allocation_index]);          //  NOLINT(cppcoreguidelines-no-malloc,hicpp-no-malloc)
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

    //  Delete all the blocks

    for (auto* block : heap_blocks)
    {
        free(block);                //  NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)
    }

    //  After exiting this function, all heap activity should be complete and there should be zero leaks.
}

void JustMalloc(std::shared_future<void>* start_sync, std::promise<void*>* malloc_promise)
{
    constexpr size_t    BLOCK_SIZE = 129;

    start_sync->wait();

    void* block = malloc(BLOCK_SIZE);          //  NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)

    malloc_promise->set_value(block);
}

void JustFree(std::shared_future<void>* start_sync, std::future<void*>* malloc_future)
{
    start_sync->wait();

    free(malloc_future->get());          //  NOLINT(cppcoreguidelines-no-malloc,hicpp-no-malloc)
}

void MallocFreeRace()
{
    std::promise<void> sync_promise;
    std::shared_future<void> sync_future1 = sync_promise.get_future();
    std::shared_future<void> sync_future2 = sync_future1;
    std::shared_future<void> sync_future3 = sync_future1;
    std::shared_future<void> sync_future4 = sync_future1;
    std::shared_future<void> sync_future5 = sync_future1;
    std::shared_future<void> sync_future6 = sync_future1;
    std::shared_future<void> sync_future7 = sync_future1;
    std::shared_future<void> sync_future8 = sync_future1;
    std::shared_future<void> sync_future9 = sync_future1;
    std::shared_future<void> sync_future10 = sync_future1;

    std::promise<void*> malloc_promise1;
    std::future<void*> malloc_future1 = malloc_promise1.get_future();
    std::promise<void*> malloc_promise2;
    std::future<void*> malloc_future2 = malloc_promise2.get_future();
    std::promise<void*> malloc_promise3;
    std::future<void*> malloc_future3 = malloc_promise3.get_future();
    std::promise<void*> malloc_promise4;
    std::future<void*> malloc_future4 = malloc_promise4.get_future();
    std::promise<void*> malloc_promise5;
    std::future<void*> malloc_future5 = malloc_promise5.get_future();

    std::thread malloc_thread1(JustMalloc, &sync_future1, &malloc_promise1);
    std::thread malloc_thread2(JustMalloc, &sync_future3, &malloc_promise2);
    std::thread malloc_thread3(JustMalloc, &sync_future5, &malloc_promise3);
    std::thread malloc_thread4(JustMalloc, &sync_future7, &malloc_promise4);
    std::thread malloc_thread5(JustMalloc, &sync_future9, &malloc_promise5);

    std::thread free_thread1(&JustFree, &sync_future2, &malloc_future1);
    std::thread free_thread2(&JustFree, &sync_future4, &malloc_future2);
    std::thread free_thread3(&JustFree, &sync_future6, &malloc_future3);
    std::thread free_thread4(&JustFree, &sync_future8, &malloc_future4);
    std::thread free_thread5(&JustFree, &sync_future10, &malloc_future5);

    std::this_thread::sleep_for(50ms);

    sync_promise.set_value();

    malloc_thread1.join();
    malloc_thread2.join();
    malloc_thread3.join();
    malloc_thread4.join();
    malloc_thread5.join();

    free_thread1.join();
    free_thread2.join();
    free_thread3.join();
    free_thread4.join();
    free_thread5.join();
}


#pragma GCC pop_options

