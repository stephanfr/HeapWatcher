#include "TestWorkloads.hpp"

#include <deque>
#include <iostream>
#include <map>
#include <random>

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

void RandomHeapOperations( long num_operations )
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