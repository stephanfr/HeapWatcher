#include "HeapWatcher.hpp"

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
#include <unordered_set>

#include "BacktraceDemangler.hpp"
#include "WorkerRequest.hpp"
#include "blockingconcurrentqueue.h"

using namespace std::literals::chrono_literals;

extern "C" void* __libc_malloc(size_t size);
extern "C" void* __libc_calloc(size_t nitems, size_t size);
extern "C" void* __libc_realloc(void* address, size_t size);
extern "C" void __libc_free(void* address);

void* heapwatcher_instrumented_malloc(size_t size);
void* heapwatcher_instrumented_calloc(size_t nitems, size_t size);
void* heapwatcher_instrumented_realloc(void* address, size_t size);
void heapwatcher_instrumented_free(void* address);

void* heapwatcher_single_thread_instrumented_malloc(size_t size);
void* heapwatcher_single_thread_instrumented_calloc(size_t nitems, size_t size);
void* heapwatcher_single_thread_instrumented_realloc(void* address, size_t size);
void heapwatcher_single_thread_instrumented_free(void* address);

namespace SEFUtility::HeapWatcher
{
    using AllocationMap = std::map<void*, AllocationRecord>;

    typedef void* (*MallocFunctionPointer)(size_t);
    typedef void* (*CallocFunctionPointer)(size_t, size_t);
    typedef void* (*ReallocFunctionPointer)(void*, size_t);
    typedef void (*FreeFunctionPointer)(void*);

    std::ostream& operator<<(std::ostream& os, const ModuleFunctionOffset& record)
    {
        os << record.address_ << "    " << record.module_ << "    " << record.function_ << "    " << record.offset_;

        return os;
    }

    std::ostream& operator<<(std::ostream& os, const std::vector<ModuleFunctionOffset>& records)
    {
        for (auto record : records)
        {
            os << record << std::endl;
        }

        return os;
    }

    class HeapWatcherImpl : public HeapWatcher
    {
       private:
        static constexpr size_t INITIAL_NUMBER_OF_BLOCKS = 32;

       public:
        static std::atomic<MallocFunctionPointer> malloc_function_;
        static std::atomic<CallocFunctionPointer> calloc_function_;
        static std::atomic<ReallocFunctionPointer> realloc_function_;
        static std::atomic<FreeFunctionPointer> free_function_;

        HeapWatcherImpl() : worker_request_queue_(INITIAL_NUMBER_OF_BLOCKS * LargeTraits::BLOCK_SIZE)
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

            //  Find out where in the stack trace the 'malloc' symbol appears

            probe_for_malloc_stack_depth();

            //  Find the known leak for std::thread thread local storage

            known_leaks_.reserve(64);

            //  Next, pthreads intentionally leak so we want to create one now and capture the address
            //      of the function that leaks.

            std::list<std::string> leaking_symbol{"_dl_allocate_tls"};

            capture_known_leak(leaking_symbol, []() {
                std::thread leaky_thread([]() { std::this_thread::sleep_for(1ms); });

                leaky_thread.join();
            });
        }

        ~HeapWatcherImpl()
        {
            worker_thread_running_ = false;
            worker_thread_.join();
        }

        bool is_watching_globally() const { return watching_globally_; }
        bool is_watching_this_thread() const { return watching_globally_ && watching_thread_; }
        bool is_known_leak(const void** stack_trace, size_t     trace_depth ) const
        {
            for( auto& known_leak : known_leaks_ )
            {
                if( known_leak.offset_from_malloc_ < trace_depth )
                {
                    if( std::find(known_leaks_.begin(), known_leaks_.end(), stack_trace[known_leak.offset_from_malloc_]) != known_leaks_.end() )
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        uint64_t capture_known_leak(std::list<std::string>& leaking_symbols,
                                    std::function<void()> function_which_leaks);

        const KnownLeaks known_leaks() const
        {
            std::vector<const void*>    leak_addresses;

            leak_addresses.reserve( known_leaks_.size() );

            for( auto leak : known_leaks_ )
            {
                leak_addresses.emplace_back( leak.address_ );
            }

            return KnownLeaks(leak_addresses);
        }

        void start_watching() final
        {
            std::lock_guard<std::mutex> guard(command_mutex_);

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

            set_multithreaded_instrumentation();
        }

        HeapSnapshot stop_watching() final
        {
            std::lock_guard<std::mutex> guard(command_mutex_);

            remove_instrumentation();

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

        //
        //  Heap functions for multi-threaded instrumentation
        //

        void* instrumented_malloc(size_t size)
        {
            void* address = __libc_malloc(size);

            if (is_watching_this_thread())
            {
                PauseThreadWatchToken pause_watching_guard;

                std::array<void*, MAX_CALLSTACK_RETAINED + MAX_MALLOC_DEPTH_IN_CALL_STACK> stack_tail;
                stack_tail.fill(nullptr);

                backtrace(stack_tail.data(), MAX_CALLSTACK_RETAINED + malloc_depth_in_call_stack_);

                if (!is_known_leak(const_cast<const void**>(stack_tail.data() + malloc_depth_in_call_stack_), stack_tail.size() - malloc_depth_in_call_stack_ ))
                {
                    worker_request_queue_.enqueue(
                        WorkerRequest::malloc_request(size, address, &stack_tail.data()[malloc_depth_in_call_stack_]));
                }
                else
                {
                    number_of_known_leaks_++;
                }
            }

            return address;
        }

        void* instrumented_calloc(size_t nitems, size_t size)
        {
            void* address = __libc_calloc(nitems, size);

            if (is_watching_this_thread())
            {
                PauseThreadWatchToken pause_watching_guard;

                std::array<void*, MAX_CALLSTACK_RETAINED + MAX_MALLOC_DEPTH_IN_CALL_STACK> stack_tail;
                stack_tail.fill(nullptr);

                backtrace(stack_tail.data(), MAX_CALLSTACK_RETAINED + malloc_depth_in_call_stack_);

                if (!is_known_leak(const_cast<const void**>( stack_tail.data() + malloc_depth_in_call_stack_), stack_tail.size() - malloc_depth_in_call_stack_ ))
                {
                    worker_request_queue_.enqueue(WorkerRequest::malloc_request(
                        size * nitems, address, &stack_tail.data()[malloc_depth_in_call_stack_]));
                }
                else
                {
                    number_of_known_leaks_++;
                }
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

        //
        //  Heap functions for single-threaded instrumentation
        //

        void* single_thread_instrumented_malloc(size_t size)
        {
            void* address = __libc_malloc(size);

            if (watching_thread_ && (std::this_thread::get_id() == watched_single_thread_id_))
            {
                PauseThreadWatchToken pause_watching_guard;

                std::array<void*, MAX_CALLSTACK_RETAINED + MAX_MALLOC_DEPTH_IN_CALL_STACK> stack_tail;
                stack_tail.fill(nullptr);

                backtrace(stack_tail.data(), MAX_CALLSTACK_RETAINED + malloc_depth_in_call_stack_);

                single_threaded_allocations_.emplace(std::make_pair(
                    address,
                    AllocationRecord(size, address, get_txn_id(), &stack_tail.data()[malloc_depth_in_call_stack_])));
            }

            return address;
        }

        void* single_thread_instrumented_calloc(size_t nitems, size_t size)
        {
            void* address = __libc_calloc(nitems, size);

            if (watching_thread_ && (std::this_thread::get_id() == watched_single_thread_id_))
            {
                PauseThreadWatchToken pause_watching_guard;

                std::array<void*, MAX_CALLSTACK_RETAINED + MAX_MALLOC_DEPTH_IN_CALL_STACK> stack_tail;
                stack_tail.fill(nullptr);

                backtrace(stack_tail.data(), MAX_CALLSTACK_RETAINED + malloc_depth_in_call_stack_);

                single_threaded_allocations_.emplace(std::make_pair(
                    address,
                    AllocationRecord(size, address, get_txn_id(), &stack_tail.data()[malloc_depth_in_call_stack_])));
            }

            return address;
        }

        void* single_thread_instrumented_realloc(void* original_address, size_t new_size)
        {
            void* new_address = __libc_realloc(original_address, new_size);

            if (watching_thread_ && (std::this_thread::get_id() == watched_single_thread_id_))
            {
                PauseThreadWatchToken pause_watching_guard;

                if (auto prior_allocation_itr = allocations_.find(original_address);
                    prior_allocation_itr != allocations_.end())
                {
                    single_threaded_allocations_.erase(original_address);

                    single_threaded_allocations_.emplace(
                        std::make_pair(new_address, AllocationRecord(new_size, new_address, get_txn_id(),
                                                                     prior_allocation_itr->second.raw_stack_trace())));
                }
            }

            return new_address;
        }

        void single_thread_instrumented_free(void* address)
        {
            __libc_free(address);

            if (watching_thread_ && (std::this_thread::get_id() == watched_single_thread_id_))
            {
                PauseThreadWatchToken pause_watching_guard;

                single_threaded_allocations_.erase(address);
            }
        }

       private:
        //
        //   Data Members
        //

        static constexpr size_t MAX_MALLOC_DEPTH_IN_CALL_STACK = 3;

        struct LargeTraits : public moodycamel::ConcurrentQueueDefaultTraits
        {
            static const size_t BLOCK_SIZE = 128;
            static const size_t INITIAL_IMPLICIT_PRODUCER_HASH_SIZE = 128;
            static const size_t IMPLICIT_INITIAL_INDEX_SIZE = 128;
        };

        std::atomic_bool watching_globally_{false};

        thread_local static bool watching_thread_;

        std::mutex command_mutex_;
        std::thread::id watched_single_thread_id_;

        friend class PauseThreadWatchToken;

        moodycamel::BlockingConcurrentQueue<WorkerRequest, LargeTraits> worker_request_queue_;

        std::thread worker_thread_;
        std::atomic_bool worker_thread_running_{false};

        std::map<const void*, AllocationRecord> single_threaded_allocations_;

        std::map<const void*, AllocationRecord> allocations_;
        std::unordered_set<void*> frees_without_mallocs_;

        struct LeakAddressAndOffset
        {
            LeakAddressAndOffset( const void*       address,
                                  int               offset )
                : address_( address ),
                  offset_from_malloc_( offset )
            {}

            bool operator==( const void*        address ) const
            {
                return address_ == address;
            }

            const void*         address_;
            const int           offset_from_malloc_;
        };

        std::vector<LeakAddressAndOffset> known_leaks_;

        size_t malloc_depth_in_call_stack_{0};

        uint64_t number_of_mallocs_{0};
        uint64_t number_of_reallocs_{0};
        uint64_t number_of_frees_{0};
        uint64_t number_of_known_leaks_{0};

        uint64_t bytes_allocated_{0};
        uint64_t bytes_freed_{0};

        //
        //  Private methods
        //

        void remove_instrumentation()
        {
            watching_globally_ = false;

            malloc_function_ = __libc_malloc;
            calloc_function_ = __libc_calloc;
            realloc_function_ = __libc_realloc;
            free_function_ = __libc_free;
        }

        void set_multithreaded_instrumentation()
        {
            malloc_function_ = heapwatcher_instrumented_malloc;
            calloc_function_ = heapwatcher_instrumented_calloc;
            realloc_function_ = heapwatcher_instrumented_realloc;
            free_function_ = heapwatcher_instrumented_free;

            watching_globally_ = true;
        }

        void set_single_threaded_instrumentation()
        {
            malloc_function_ = heapwatcher_single_thread_instrumented_malloc;
            calloc_function_ = heapwatcher_single_thread_instrumented_calloc;
            realloc_function_ = heapwatcher_single_thread_instrumented_realloc;
            free_function_ = heapwatcher_single_thread_instrumented_free;

            watching_globally_ = true;
        }

        class PauseThreadWatchToken : public SEFUtility::HeapWatcher::PauseThreadWatchToken
        {
           public:
            PauseThreadWatchToken() : saved_value_(watching_thread_) { watching_thread_ = false; }
            PauseThreadWatchToken(bool saved_value) : saved_value_(saved_value) { watching_thread_ = false; }

            ~PauseThreadWatchToken() { watching_thread_ = saved_value_; }

           private:
            const bool saved_value_;
        };

        void probe_for_malloc_stack_depth();

        //
        //  'Main' function for tracking allocations
        //

        void worker_main();
    };

    //
    //  Globals
    //

    std::atomic<MallocFunctionPointer> HeapWatcherImpl::malloc_function_{__libc_malloc};
    std::atomic<CallocFunctionPointer> HeapWatcherImpl::calloc_function_{__libc_calloc};
    std::atomic<ReallocFunctionPointer> HeapWatcherImpl::realloc_function_{__libc_realloc};
    std::atomic<FreeFunctionPointer> HeapWatcherImpl::free_function_{__libc_free};

    HeapWatcherImpl heap_watcher_;
    thread_local bool HeapWatcherImpl::watching_thread_{true};

    HeapWatcher& get_heap_watcher() { return heap_watcher_; }

    //
    //  HeapWatcherImpl implementation
    //

    void single_leak_probe_function(void*& leaked_memory) { leaked_memory = malloc(16); }

    void HeapWatcherImpl::probe_for_malloc_stack_depth()
    {
        //  Clear the record of allocations, then launch a separate thread within which we
        //      set the thread id and then swap in the single thread instrumented functions
        //      and then run the leaking code.  After the leaking code finishes, we yank out
        //      the instrumented functions.

        void* leaked_memory;

        single_threaded_allocations_.clear();

        watched_single_thread_id_ = std::this_thread::get_id();

        set_single_threaded_instrumentation();

        {
            single_leak_probe_function(leaked_memory);
        }

        remove_instrumentation();

        //  Free the probe memory we leaked.

        free(leaked_memory);

        //  There should be only a single leak

        assert(single_threaded_allocations_.size() == 1);

        std::vector<const void*> leak_stack_trace;

        leak_stack_trace.reserve(single_threaded_allocations_.begin()->second.raw_stack_trace().size());

        for (auto address : single_threaded_allocations_.begin()->second.raw_stack_trace())
        {
            leak_stack_trace.emplace_back(address);
        }

        auto leak_symbols = symbols_for_addresses(leak_stack_trace);

        //  Find 'malloc' in the stack trace

        for (int i = 0; i < leak_symbols.size(); i++)
        {
            if (leak_symbols[i].function() == "malloc")
            {
                malloc_depth_in_call_stack_ = i + 1;
                break;
            }
        }

        //  Clear the single threaded leak collection

        single_threaded_allocations_.clear();
    }

    uint64_t HeapWatcherImpl::capture_known_leak(std::list<std::string>& leaking_symbols,
                                                 std::function<void()> function_which_leaks)
    {
        //  Make sure this is the only thing the HeapWatcher is doing and that we are not already
        //      watching globally.

        std::lock_guard<std::mutex> guard(command_mutex_);

        if (watching_globally_)
        {
            return -1;
        }

        //  Clear the record of allocations, then launch a separate thread within which we
        //      set the thread id and then swap in the single thread instrumented functions
        //      and then run the leaking code.  After the leaking code finishes, we yank out
        //      the instrumented functions.

        single_threaded_allocations_.clear();

        auto leaking_thread = std::async(std::launch::async, [this, function_which_leaks]() {
            watched_single_thread_id_ = std::this_thread::get_id();

            set_single_threaded_instrumentation();

            {
                function_which_leaks();
            }

            remove_instrumentation();
        });

        leaking_thread.wait();

        //  Get the symbols for the leak(s) so that we can filter on specific function names

        std::vector<const void*> leaks_found;

        leaks_found.reserve(single_threaded_allocations_.size());

//        for (auto leak : single_threaded_allocations_)
//        {
//            leaks_found.emplace_back(leak.second.raw_stack_trace()[0]);
//        }

//        auto leaking_symbols_found = symbols_for_addresses(leaks_found);

        //  Finally, pass through the leaks and we will only keep those that match names
        //      passed into the function.

        int number_of_leaks_found = 0;
        for (auto leak : single_threaded_allocations_)
        {
            auto leaking_stack_trace =  leak.second.stack_trace();

            if (!is_known_leak(const_cast<const void**>(leak.second.raw_stack_trace().data()), leak.second.raw_stack_trace().size()))
            {
                for( auto offset = 0; offset < leaking_stack_trace.size(); offset++ )
                {
                    if (std::find(leaking_symbols.begin(), leaking_symbols.end(), leaking_stack_trace[offset].function()) !=
                        leaking_symbols.end())
                    {
                        known_leaks_.emplace_back(LeakAddressAndOffset( leak.second.raw_stack_trace()[offset], offset ));
                        number_of_leaks_found++;
                        break;
                    }
                }
            }
        }

        //  Clear the single threaded leak collection and return the number of leaks we found and filtered.

        single_threaded_allocations_.clear();

        return number_of_leaks_found;
    }

    void HeapWatcherImpl::worker_main()
    {
        constexpr size_t MAX_REQUESTS_TO_DEQUEUE = 20;

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

                            AllocationRecord revised_record(requests[i].realloc_record().new_size_,
                                                            requests[i].realloc_record().new_address_,
                                                            record->second.txn_id(), record->second.raw_stack_trace());

                            allocations_.erase(record);

                            allocations_.emplace(revised_record.address(), revised_record);
                        }
                        else
                        {
                            AllocationRecord allocation_record(
                                requests[i].realloc_record().new_size_, requests[i].realloc_record().new_address_,
                                record->second.txn_id(), record->second.raw_stack_trace());

                            allocations_.emplace(requests[i].realloc_record().new_address_, allocation_record);
                        }
                    }
                    break;

                    case WorkerOperation::FREE_REQUEST:
                    {
                        auto iterator = allocations_.find(requests[i].block_to_free());

                        if (iterator != allocations_.end())
                        {
                            number_of_frees_++;
                            bytes_freed_ += iterator->second.size();
                            allocations_.erase(iterator);
                        }
                        else
                        {
                            frees_without_mallocs_.emplace(requests[i].block_to_free());
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
                        frees_without_mallocs_.clear();

                        requests[i].clear_allocations_promise().set_value();
                    }
                    break;

                    case WorkerOperation::GET_ALLOCATION_SNAPSHOT:
                    {
                        //  First, check for any mallocs that may have been caught in
                        //      race conditions with their frees.  This can sometimes happen
                        //      with copies of stack elements which allocate heap space internally,
                        //      like std::string.

                        for (auto current_free : frees_without_mallocs_)
                        {
                            auto iterator = allocations_.find(current_free);

                            if (iterator != allocations_.end())
                            {
                                number_of_frees_++;
                                bytes_freed_ += iterator->second.size();
                                allocations_.erase(iterator);
                            }
                        }

                        std::unique_ptr<AllocationVector> snapshot(new AllocationVector());

                        snapshot->reserve(allocations_.size());

                        for (const auto& entry : allocations_)
                        {
                            snapshot->emplace_back(entry.second);
                        }

                        requests[i].allocation_snapshot_promise().set_value(
                            HeapSnapshot(number_of_mallocs_, number_of_reallocs_, number_of_frees_,
                                         number_of_known_leaks_, bytes_allocated_, bytes_freed_, snapshot));
                    }
                    break;

                    case WorkerOperation::GET_HIGH_LEVEL_STATS:
                    {
                        requests[i].high_level_statistics_promise().set_value(
                            HighLevelStatistics(number_of_mallocs_, number_of_reallocs_, number_of_frees_,
                                                number_of_known_leaks_, bytes_allocated_, bytes_freed_));
                    }
                    break;
                }
            }
        }
    }

}  // namespace SEFUtility::HeapWatcher

//
//  Wrapper functions used to redirect calls to the heap functions through function pointers
//

void* heapwatcher_instrumented_malloc(size_t size)
{
    return SEFUtility::HeapWatcher::heap_watcher_.instrumented_malloc(size);
}

void* heapwatcher_instrumented_calloc(size_t nitems, size_t size)
{
    return SEFUtility::HeapWatcher::heap_watcher_.instrumented_calloc(nitems, size);
}

void* heapwatcher_instrumented_realloc(void* address, size_t size)
{
    return SEFUtility::HeapWatcher::heap_watcher_.instrumented_realloc(address, size);
}

void heapwatcher_instrumented_free(void* address)
{
    return SEFUtility::HeapWatcher::heap_watcher_.instrumented_free(address);
}

//
//  Wrapper functions used to redirect calls to the heap functions through function pointers
//

void* heapwatcher_single_thread_instrumented_malloc(size_t size)
{
    return SEFUtility::HeapWatcher::heap_watcher_.single_thread_instrumented_malloc(size);
}

void* heapwatcher_single_thread_instrumented_calloc(size_t nitems, size_t size)
{
    return SEFUtility::HeapWatcher::heap_watcher_.single_thread_instrumented_calloc(nitems, size);
}

void* heapwatcher_single_thread_instrumented_realloc(void* address, size_t size)
{
    return SEFUtility::HeapWatcher::heap_watcher_.single_thread_instrumented_realloc(address, size);
}

void heapwatcher_single_thread_instrumented_free(void* address)
{
    return SEFUtility::HeapWatcher::heap_watcher_.single_thread_instrumented_free(address);
}

//
//  Overrides of the C heap functions follow
//
//  We do not want these inlined so that we can identify the depth in the call stack that we
//      can ignore for allocations.
//

#pragma GCC push_options
#pragma GCC optimize("O0")

void* malloc(size_t size) { return SEFUtility::HeapWatcher::HeapWatcherImpl::malloc_function_.load()(size); }

void* calloc(size_t nitems, size_t size)
{
    return SEFUtility::HeapWatcher::HeapWatcherImpl::calloc_function_.load()(nitems, size);
}

void* realloc(void* address, size_t size)
{
    return SEFUtility::HeapWatcher::HeapWatcherImpl::realloc_function_.load()(address, size);
}

void free(void* address) { return SEFUtility::HeapWatcher::HeapWatcherImpl::free_function_.load()(address); }

#pragma gcc pop_options