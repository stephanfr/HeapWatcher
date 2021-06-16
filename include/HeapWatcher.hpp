#pragma once

#include <cstring>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <vector>

namespace SEFUtility::HeapWatcher
{
#ifdef MAX_CALLSTACK_DEPTH_RETAINED
    static constexpr int MAX_CALLSTACK_RETAINED = MAX_CALLSTACK_DEPTH_RETAINED;
#else
    static constexpr int MAX_CALLSTACK_RETAINED = 4;
#endif

    class HighLevelStatistics
    {
       public:
        HighLevelStatistics(const HighLevelStatistics& stats_to_copy) = default;

        HighLevelStatistics(HighLevelStatistics&& stats_to_move) = default;

        ~HighLevelStatistics() = default;

        HighLevelStatistics& operator=(const HighLevelStatistics&) = delete;
        HighLevelStatistics& operator=(HighLevelStatistics&&) = delete;

        [[nodiscard]] uint64_t number_of_mallocs() const { return number_of_mallocs_; }
        [[nodiscard]] uint64_t number_of_reallocs() const { return number_of_reallocs_; }
        [[nodiscard]] uint64_t number_of_frees() const { return number_of_frees_; }
        [[nodiscard]] uint64_t number_of_known_leaks() const { return number_of_known_leaks_; }

        [[nodiscard]] uint64_t bytes_allocated() const { return bytes_allocated_; }
        [[nodiscard]] uint64_t bytes_freed() const { return bytes_freed_; }

       private:
        HighLevelStatistics(uint64_t number_of_mallocs, uint64_t number_of_reallocs, uint64_t number_of_frees,
                            uint64_t number_of_known_leaks, uint64_t bytes_allocated, uint64_t bytes_freed)
            : number_of_mallocs_(number_of_mallocs),
              number_of_reallocs_(number_of_reallocs),
              number_of_frees_(number_of_frees),
              number_of_known_leaks_(number_of_known_leaks),
              bytes_allocated_(bytes_allocated),
              bytes_freed_(bytes_freed)
        {
        }

        const uint64_t number_of_mallocs_;
        const uint64_t number_of_reallocs_;
        const uint64_t number_of_frees_;
        const uint64_t number_of_known_leaks_;

        const uint64_t bytes_allocated_;
        const uint64_t bytes_freed_;

        friend class HeapSnapshot;
        friend class HeapWatcherImpl;
    };

    class ModuleFunctionOffset
    {
       public:
        ModuleFunctionOffset(std::string address, std::string module, std::string function, std::string offset)
            : address_(std::move(address)),
              module_(std::move(module)),
              function_(std::move(function)),
              offset_(std::move(offset))
        {
        }

        [[nodiscard]] const std::string& address() const { return address_; }
        [[nodiscard]] const std::string& module() const { return module_; }
        [[nodiscard]] const std::string& function() const { return function_; }
        [[nodiscard]] const std::string& offset() const { return offset_; }

       private:
        const std::string address_;
        const std::string module_;
        const std::string function_;
        const std::string offset_;

        friend std::ostream& operator<<( std::ostream& os, const ModuleFunctionOffset& record);
    };

    std::ostream& operator<<( std::ostream& os, const std::vector<ModuleFunctionOffset>& records);

    class AllocationRecord
    {
       public:
        AllocationRecord() = delete;

        [[nodiscard]] size_t size() const { return size_; }
        [[nodiscard]] const void* address() const { return address_; }
        [[nodiscard]] uint64_t txn_id() const { return txn_id_; }
        [[nodiscard]] const std::array<const void*, MAX_CALLSTACK_RETAINED>& raw_stack_trace() const { return stack_tail_; }

        [[nodiscard]] const std::vector<ModuleFunctionOffset> stack_trace() const;

       private:
        const size_t size_;
        const void* address_;
        const uint64_t txn_id_;
        const std::array<const void*, MAX_CALLSTACK_RETAINED> stack_tail_;

        //  NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init, hicpp-member-init)
        AllocationRecord(size_t size, void* address, uint32_t txn_id,
                         std::array<const void*, MAX_CALLSTACK_RETAINED> stack_tail)
            : size_(size), address_(address), txn_id_(txn_id), stack_tail_(stack_tail)
        {
        }

        AllocationRecord(size_t size, void* address, uint32_t txn_id, void* const* stack_tail)
            : size_(size),
              address_(address),
              txn_id_(txn_id),
              stack_tail_(*(reinterpret_cast<const std::array<const void*, MAX_CALLSTACK_RETAINED>*>(stack_tail)))
        {
        }

        friend class HeapWatcherImpl;
        friend class WorkerRequest;
    };

    using AllocationVector = std::vector<AllocationRecord>;

    class HeapSnapshot
    {
       public:
        HeapSnapshot() = delete;
        HeapSnapshot(const HeapSnapshot&) = delete;

        HeapSnapshot(HeapSnapshot&& snapshot_to_move) noexcept
            : high_level_statistics_(snapshot_to_move.high_level_statistics_),
              open_allocations_(std::move(snapshot_to_move.open_allocations_))
        {
        }

        ~HeapSnapshot() = default;

        HeapSnapshot& operator=(const HeapSnapshot&) = delete;
        HeapSnapshot& operator=(HeapSnapshot&& snapshot_to_move) = delete;

        [[nodiscard]] bool has_leaks() const { return !open_allocations_->empty(); }
        [[nodiscard]] size_t numberof_leaks() const { return open_allocations_->size(); }

        [[nodiscard]] const HighLevelStatistics& high_level_statistics() const { return high_level_statistics_; }

        [[nodiscard]] const AllocationVector& open_allocations() const { return *open_allocations_; }

       private:
        HeapSnapshot(uint64_t number_of_mallocs, uint64_t number_of_reallocs, uint64_t number_of_frees,
                     uint64_t number_of_known_leaks, uint64_t bytes_allocated, uint64_t bytes_freed,
                     std::unique_ptr<AllocationVector>& open_allocations)  // NOLINT(google-runtime-references)
            : high_level_statistics_(number_of_mallocs, number_of_reallocs, number_of_frees, number_of_known_leaks,
                                     bytes_allocated, bytes_freed),
              open_allocations_(open_allocations.release())
        {
        }

        const HighLevelStatistics high_level_statistics_;
        std::unique_ptr<const AllocationVector> open_allocations_;

        friend class HeapWatcherImpl;
    };

    class KnownLeaks
    {
       public:
        KnownLeaks(const KnownLeaks& known_leaks) : known_leaks_(known_leaks.known_leaks_) {}

        KnownLeaks(KnownLeaks&& known_leaks) : known_leaks_(std::move(known_leaks.known_leaks_)) {}

        [[nodiscard]] const std::vector<const void*>& addresses() const { return known_leaks_; }

        [[nodiscard]] const std::vector<ModuleFunctionOffset> symbols() const;

       private:
        KnownLeaks(const std::vector<const void*>& known_leaks) : known_leaks_(known_leaks) {}

        const std::vector<const void*> known_leaks_;

        friend class HeapWatcherImpl;
    };

    class PauseThreadWatchToken
    {
       public:
        PauseThreadWatchToken(const PauseThreadWatchToken&) = delete;
        PauseThreadWatchToken(PauseThreadWatchToken&&) = delete;
        PauseThreadWatchToken& operator=(const PauseThreadWatchToken&) = delete;
        PauseThreadWatchToken& operator=(PauseThreadWatchToken&&) = delete;

        virtual ~PauseThreadWatchToken() = default;

       protected:
        PauseThreadWatchToken() = default;
    };

    class PauseThreadWatchGuard
    {
       public:
        PauseThreadWatchGuard() = delete;
        explicit PauseThreadWatchGuard(const PauseThreadWatchGuard&) = delete;

        explicit PauseThreadWatchGuard(std::unique_ptr<PauseThreadWatchToken> token) : token_(token.release()) {}

        PauseThreadWatchGuard(PauseThreadWatchGuard&& guard_to_move) noexcept : token_(guard_to_move.token_.release())
        {
        }

        ~PauseThreadWatchGuard() = default;

        PauseThreadWatchGuard& operator=(const PauseThreadWatchGuard&) = delete;
        PauseThreadWatchGuard& operator=(PauseThreadWatchGuard&&) = delete;

       private:
        std::unique_ptr<PauseThreadWatchToken> token_;
    };

    class HeapWatcher
    {
       public:
        virtual void start_watching() = 0;
        virtual HeapSnapshot stop_watching() = 0;

        virtual uint64_t capture_known_leak(std::list<std::string>& leaking_symbols, std::function<void()> function_which_leaks) = 0;
        [[nodiscard]] virtual const KnownLeaks known_leaks() const = 0;

        [[nodiscard]] virtual PauseThreadWatchGuard pause_watching_this_thread() = 0;

        [[nodiscard]] virtual const HeapSnapshot snapshot() = 0;
        [[nodiscard]] virtual const HighLevelStatistics high_level_stats() = 0;
    };

    HeapWatcher& get_heap_watcher();

}  // namespace SEFUtility::HeapWatcher
