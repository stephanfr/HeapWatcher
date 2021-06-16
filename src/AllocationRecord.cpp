#include "HeapWatcher.hpp"

#include "BacktraceDemangler.hpp"

namespace SEFUtility::HeapWatcher
{
    const std::vector<ModuleFunctionOffset> AllocationRecord::stack_trace() const
    {
        PauseThreadWatchGuard pause_watching_guard(std::move(get_heap_watcher().pause_watching_this_thread()));

        std::vector<const void*> addresses;

        addresses.reserve(MAX_CALLSTACK_RETAINED);

        //  First, determine how many entries we have in the trace.  Uninitialized
        //      entries will be nullptrs;

        int number_of_tail_entries = 0;

        while ((stack_tail_[number_of_tail_entries] != nullptr) && (number_of_tail_entries < MAX_CALLSTACK_RETAINED))
        {
            addresses.emplace_back( stack_tail_[number_of_tail_entries++] );
        }

        return (std::move( symbols_for_addresses( addresses ) ));
    }

}  // namespace SEFUtility::HeapWatcher
