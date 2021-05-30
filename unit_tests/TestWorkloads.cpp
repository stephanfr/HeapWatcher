#include "TestWorkloads.hpp"

#include <deque>
#include <future>
#include <iostream>
#include <map>
#include <random>

using namespace std::literals::chrono_literals;

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

void RandomHeapOperations(long num_operations)
{
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

    for (int i = 0; i < num_operations; i++)
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

    //  Delete all the blocks

    for (auto block : heap_blocks)
    {
        free(block);
    }

    //  After exiting this function, all heap activity should be complete and there should be zero leaks.
}

void JustMalloc(std::shared_future<void>* start_sync, std::promise<void*>* malloc_promise)
{
    start_sync->wait();

    void* block = malloc(513);

    malloc_promise->set_value(block);
}

void JustFree(std::shared_future<void>* start_sync, std::future<void*>* malloc_future)
{
    start_sync->wait();

    free(malloc_future->get());
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
