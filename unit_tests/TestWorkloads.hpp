#pragma once

#include <future>

void NoLeaks();
void OneLeak();
void OneLeakNested();
void NoLeaksWithRealloc();
void OneLeakWithRealloc();
void BuildBigMap();
void RandomHeapOperations( int64_t num_operations );

void JustMalloc( std::shared_future<void>*  start_sync, std::promise<void*>*     malloc_promise );
void JustFree( std::shared_future<void>*   start_sync, std::future<void*>*     malloc_future );
void MallocFreeRace();
