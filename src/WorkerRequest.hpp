#pragma once

#include <cstring>
#include <future>

#include "HeapWatcher.hpp"

namespace SEFUtility::HeapWatcher
{
    uint64_t get_txn_id()
    {
        static std::atomic<uint64_t> g_txn_id{1};
        return g_txn_id.fetch_add(1);
    }

    enum class WorkerOperation : uint8_t
    {
        UNINITIALIZED = 0,
        MALLOC_REQUEST,
        REALLOC_REQUEST,
        FREE_REQUEST,
        CLEAR_ALLOCATION_MAP,
        GET_ALLOCATION_SNAPSHOT,
        GET_HIGH_LEVEL_STATS
    };

    struct ReallocRecord
    {
        void* original_address_;
        void* new_address_;
        size_t new_size_;
    };

    //
    //  WorkerRequest class follows
    //
    //  This class is a little messy as I want to keep it small enough to fit in a single cache line, thus
    //      the union below that combines all the different data payloads into a single block.  Also,
    //      all of the elements below should simply be data blocks that can be copied with std::memcpy().
    //      References and pointers fall into that category.
    //
    //  There are static methods which wrap private constructors which set the operation data member properly
    //      with the right data fields.  These are not really necessary and make the class longer in terms of 
    //      declaration but reduce risk of accidentally creating the wrong kind of worker request.
    //

    class WorkerRequest
    {
       public:
        WorkerRequest() : operation_(WorkerOperation::UNINITIALIZED)
        {}

        WorkerRequest(const WorkerRequest& request) : operation_(request.operation_)
        {
            std::memcpy(&allocation_record_, &request.allocation_record_, sizeof(AllocationRecord));
        }

        //
        //  Static factory methods follow
        //

        static WorkerRequest malloc_request(size_t size, void* address, void* const* stack_tail)
        {
            return WorkerRequest(size, address, stack_tail);
        }

        static WorkerRequest realloc_request(void* original_address, void* new_address, size_t new_size)
        {
            return WorkerRequest(original_address, new_address, new_size);
        }

        static WorkerRequest free_request(void* address) { return WorkerRequest(address); }

        static WorkerRequest clear_allocation_map(std::promise<void>& clear_allocations_promise)
        {
            return WorkerRequest(clear_allocations_promise);
        }

        static WorkerRequest snapshot_request(std::promise<HeapSnapshot>& snapshot_promise)
        {
            return WorkerRequest(snapshot_promise);
        }

        static WorkerRequest high_level_statistics_request(std::promise<HighLevelStatistics>& stats_promise)
        {
            return WorkerRequest(stats_promise);
        }

        //
        //  Destructor and assignment operator
        //

        ~WorkerRequest() {}

        WorkerRequest& operator=(const WorkerRequest& request)
        {
            operation_ = request.operation_;
            std::memcpy(&allocation_record_, &request.allocation_record_, sizeof(AllocationRecord));

            return *this;
        }

        //
        //  Accessors
        //

        WorkerOperation operation() const { return operation_; }

        AllocationRecord& allocation_record() { return allocation_record_; }

        ReallocRecord& realloc_record() { return realloc_record_; }

        void* block_to_free() { return block_to_free_; }

        std::promise<void>& clear_allocations_promise() { return clear_allocations_promise_; }

        std::promise<HeapSnapshot>& allocation_snapshot_promise() { return allocation_snapshot_promise_; }

        std::promise<HighLevelStatistics>& high_level_statistics_promise() { return high_level_statistics_promise_; }


       private:

        //
        //  Data Members
        //

        WorkerOperation operation_;
        union
        {
            AllocationRecord allocation_record_;
            ReallocRecord realloc_record_;
            void* block_to_free_;
            std::reference_wrapper<std::promise<void>> clear_allocations_promise_;
            std::reference_wrapper<std::promise<HeapSnapshot>> allocation_snapshot_promise_;
            std::reference_wrapper<std::promise<HighLevelStatistics>> high_level_statistics_promise_;
        };

        //
        //  Constructors
        //

        WorkerRequest(WorkerOperation operation) : operation_(operation) {}

        WorkerRequest(size_t size, void* address, void* const* stack_tail)
            : operation_(WorkerOperation::MALLOC_REQUEST), allocation_record_(size, address, get_txn_id(), stack_tail)
        {
        }

        WorkerRequest(void* original_address, void* new_address, size_t new_size)
            : operation_(WorkerOperation::REALLOC_REQUEST), realloc_record_({original_address, new_address, new_size})
        {
        }

        WorkerRequest(void* address) : operation_(WorkerOperation::FREE_REQUEST), block_to_free_(address) {}

        WorkerRequest(std::promise<void>& clear_allocations_promise)
            : operation_(WorkerOperation::CLEAR_ALLOCATION_MAP), clear_allocations_promise_(clear_allocations_promise)
        {
        }

        WorkerRequest(std::promise<HeapSnapshot>& snapshot_promise)
            : operation_(WorkerOperation::GET_ALLOCATION_SNAPSHOT), allocation_snapshot_promise_(snapshot_promise)
        {
        }

        WorkerRequest(std::promise<HighLevelStatistics>& stats_promise)
            : operation_(WorkerOperation::GET_HIGH_LEVEL_STATS), high_level_statistics_promise_(stats_promise)
        {
        }
    };

}  // namespace SEFUtils::HeapWatcher
