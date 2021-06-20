#pragma once

#include <future>

constexpr size_t NUMBER_OF_LONGS_IN_ONE_CALLOC_LEAK = 9;

void KnownLeak();
void KnownLeak2();
void NestedKnownLeak2();

void NoLeaks();
void OneLeak();
void OneLeakNested();
void OneCallocLeak();
void NoLeaksWithRealloc();
void OneLeakWithRealloc();
void BuildBigMap();
void RandomHeapOperations( int64_t num_operations );

void JustMalloc( std::shared_future<void>*  start_sync, std::promise<void*>*     malloc_promise );
void JustFree( std::shared_future<void>*   start_sync, std::future<void*>*     malloc_future );
void MallocFreeRace();
