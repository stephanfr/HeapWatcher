#include "HeapWatcher.hpp"
#include "WorkerRequest.hpp"

#include <execinfo.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <list>
#include <map>
#include <memory>

#include "blockingconcurrentqueue.h"

using namespace std::literals::chrono_literals;

extern "C" void* __libc_malloc(size_t size);
extern "C" void* __libc_realloc(void* address, size_t size);
extern "C" void __libc_free(void* address);

namespace SEFUtils::HeapWatcher
{
    using AllocationMap = std::map<void*, AllocationRecord>;

    class HeapWatcherImpl : public HeapWatcher
    {
       public:
        HeapWatcherImpl()
        {
            //  Get the stack tail once now - this will force loading glibc and
            //      prevent recursive calls to malloc later in the instrumented_malloc() method.

            std::array<void*, MAX_CALLSTACK_RETAINED> stack_tail;
            backtrace(stack_tail.data(), MAX_CALLSTACK_RETAINED);

            worker_thread_ = std::thread(&HeapWatcherImpl::worker_main, this);
            while (!worker_thread_running_)
            {
                std::this_thread::sleep_for(0.1s);
            }
        }

        ~HeapWatcherImpl()
        {
            worker_thread_running_ = false;
            worker_thread_.join();
        }

        bool is_watching_globally() const { return watching_globally_; }
        bool is_watching_this_thread() const { return watching_globally_ && watching_thread_; }

        void start_watching() final
        {
            if (watching_globally_)
            {
                return;
            }

            //  Scoping below is to insure automatic variables are destroyed before global
            //      watching is set true again.

            {
                std::promise<void> clear_allocations_promise;
                std::future<void> clear_allocations_future = clear_allocations_promise.get_future();

                worker_request_queue_.enqueue(WorkerRequest::clear_allocation_map(clear_allocations_promise));

                clear_allocations_future.wait();
            }

            watching_globally_ = true;
        }

        const HeapSnapshot stop_watching() final
        {
            watching_globally_ = false;

            std::promise<HeapSnapshot> snapshot_promise;
            std::future<HeapSnapshot> snapshot_future = snapshot_promise.get_future();

            worker_request_queue_.enqueue(WorkerRequest::snapshot_request(snapshot_promise));

            return std::move(snapshot_future.get());
        }

        PauseThreadWatchGuard pause_watching_this_thread()
        {
            bool current_thread_watch_value = watching_thread_;

            watching_thread_ = false;

            std::unique_ptr<PauseThreadWatchToken> token(new PauseThreadWatchToken(current_thread_watch_value));

            return (std::move(PauseThreadWatchGuard(std::move(token))));
        }

        const HeapSnapshot snapshot() final
        {
            PauseThreadWatchToken pause_watching_guard;

            std::promise<HeapSnapshot> snapshot_promise;
            std::future<HeapSnapshot> snapshot_future = snapshot_promise.get_future();

            worker_request_queue_.enqueue(WorkerRequest::snapshot_request(snapshot_promise));

            return std::move(snapshot_future.get());
        }

        const HighLevelStatistics high_level_stats() final
        {
            PauseThreadWatchToken pause_watching_guard;

            std::promise<HighLevelStatistics> stats_promise;
            std::future<HighLevelStatistics> stats_future = stats_promise.get_future();

            worker_request_queue_.enqueue(WorkerRequest::high_level_statistics_request(stats_promise));

            return std::move(stats_future.get());
        }

        void* instrumented_malloc(size_t size)
        {
            void* address = __libc_malloc(size);

            if (is_watching_this_thread())
            {
                PauseThreadWatchToken pause_watching_guard;

                std::array<void*, SEFUtils::HeapWatcher::MAX_CALLSTACK_RETAINED + 2> stack_tail;
                stack_tail.fill(nullptr);

                backtrace(stack_tail.data(), SEFUtils::HeapWatcher::MAX_CALLSTACK_RETAINED + 2);

                worker_request_queue_.enqueue(WorkerRequest::malloc_request(size, address, stack_tail.data() + 2));
            }

            return address;
        }

        void* instrumented_realloc(void* original_address, size_t new_size)
        {
            void* new_address = __libc_realloc(original_address, new_size);

            if (is_watching_this_thread())
            {
                PauseThreadWatchToken pause_watching_guard;

                worker_request_queue_.enqueue(WorkerRequest::realloc_request(original_address, new_address, new_size));
            }

            return new_address;
        }

        void instrumented_free(void* address)
        {
            __libc_free(address);

            if (is_watching_this_thread())
            {
                PauseThreadWatchToken pause_watching_guard;

                worker_request_queue_.enqueue(WorkerRequest::free_request(address));
            }
        }

       private:
        std::atomic_bool watching_globally_{false};

        thread_local static bool watching_thread_;

        friend class PauseThreadWatchToken;

        moodycamel::BlockingConcurrentQueue<WorkerRequest> worker_request_queue_;

        std::thread worker_thread_;
        std::atomic_bool worker_thread_running_{false};

        std::map<void*, AllocationRecord> allocations_;

        uint64_t number_of_mallocs_{0};
        uint64_t number_of_reallocs_{0};
        uint64_t number_of_frees_{0};

        uint64_t bytes_allocated_{0};
        uint64_t bytes_freed_{0};

        class PauseThreadWatchToken : public SEFUtils::HeapWatcher::PauseThreadWatchToken
        {
           public:
            PauseThreadWatchToken() : saved_value_(watching_thread_) { watching_thread_ = false; }
            PauseThreadWatchToken(bool saved_value) : saved_value_(saved_value) { watching_thread_ = false; }

            ~PauseThreadWatchToken() { watching_thread_ = saved_value_; }

           private:
            const bool saved_value_;
        };

        //
        //  'Main' function for tracking allocations
        //

        void worker_main();
    };

    //
    //  Globals
    //

    HeapWatcherImpl heap_watcher_;
    thread_local bool HeapWatcherImpl::watching_thread_{true};

    HeapWatcher& get_heap_watcher() { return heap_watcher_; }

    //
    //  HeapWatcherImpl implementation
    //

    void HeapWatcherImpl::worker_main()
    {
        constexpr size_t MAX_REQUESTS_TO_DEQUEUE = 10;

        watching_thread_ = false;
        worker_thread_running_ = true;

        std::array<WorkerRequest, MAX_REQUESTS_TO_DEQUEUE> requests;

        while (worker_thread_running_)
        {
            int num_requests =
                worker_request_queue_.wait_dequeue_bulk_timed(requests.data(), MAX_REQUESTS_TO_DEQUEUE, 0.5s);

            for (int i = 0; i < num_requests; i++)
            {
                switch (requests[i].operation())
                {
                    case WorkerOperation::MALLOC_REQUEST:
                    {
                        number_of_mallocs_++;
                        bytes_allocated_ += requests[i].allocation_record().size();

                        allocations_.emplace(requests[i].allocation_record().address(),
                                             requests[i].allocation_record());
                    }
                    break;

                    case WorkerOperation::REALLOC_REQUEST:
                    {
                        number_of_reallocs_++;
                        auto record = allocations_.find(requests[i].realloc_record().original_address_);

                        if (record != allocations_.end())
                        {
                            bytes_allocated_ -= record->second.size();
                            bytes_allocated_ += requests[i].realloc_record().new_size_;

                            AllocationRecord revised_record(
                                requests[i].realloc_record().new_size_, requests[i].realloc_record().new_address_,
                                record->second.txn_id(), record->second.raw_stack_trace().data());

                            allocations_.erase(record);

                            allocations_.emplace(revised_record.address(), revised_record);
                        }
                        else
                        {
                            AllocationRecord allocation_record(
                                requests[i].realloc_record().new_size_, requests[i].realloc_record().new_address_,
                                record->second.txn_id(), record->second.raw_stack_trace().data());

                            allocations_.emplace(requests[i].realloc_record().new_address_, allocation_record);
                        }
                    }
                    break;

                    case WorkerOperation::FREE_REQUEST:
                    {
                        number_of_frees_++;
                        auto iterator = allocations_.find(requests[i].block_to_free());

                        if (iterator != allocations_.end())
                        {
                            bytes_freed_ += iterator->second.size();
                            allocations_.erase(iterator);
                        }
                    }
                    break;

                    case WorkerOperation::CLEAR_ALLOCATION_MAP:
                    {
                        number_of_mallocs_ = 0;
                        number_of_reallocs_ = 0;
                        number_of_frees_ = 0;
                        bytes_allocated_ = 0;
                        bytes_freed_ = 0;

                        allocations_.clear();
                        requests[i].clear_allocations_promise().set_value();
                    }
                    break;

                    case WorkerOperation::GET_ALLOCATION_SNAPSHOT:
                    {
                        std::unique_ptr<AllocationVector> snapshot(new AllocationVector());

                        snapshot->reserve(allocations_.size());

                        for (const auto& entry : allocations_)
                        {
                            snapshot->emplace_back(entry.second);
                        }

                        requests[i].allocation_snapshot_promise().set_value(
                            HeapSnapshot(number_of_mallocs_, number_of_reallocs_, number_of_frees_, bytes_allocated_,
                                         bytes_freed_, snapshot));
                    }
                    break;

                    case WorkerOperation::GET_HIGH_LEVEL_STATS:
                    {
                        requests[i].high_level_statistics_promise().set_value(HighLevelStatistics(
                            number_of_mallocs_, number_of_reallocs_, number_of_frees_, bytes_allocated_, bytes_freed_));
                    }
                    break;
                }
            }
        }
    }

}  // namespace SEFUtils::HeapWatcher

//
//  Overrides of the C heap functions follow
//

void* malloc(size_t size) { return SEFUtils::HeapWatcher::heap_watcher_.instrumented_malloc(size); }

void* realloc(void* address, size_t size)
{
    return SEFUtils::HeapWatcher::heap_watcher_.instrumented_realloc(address, size);
}

void free(void* address) { SEFUtils::HeapWatcher::heap_watcher_.instrumented_free(address); }
