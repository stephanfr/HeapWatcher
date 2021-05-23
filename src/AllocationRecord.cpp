#include "HeapWatcher.hpp"

#include <cxxabi.h>
#include <execinfo.h>

namespace SEFUtils::HeapWatcher
{
    const std::vector<ModuleFunctionOffset> AllocationRecord::stack_trace() const
    {
        PauseThreadWatchGuard   pause_watching_guard( std::move( get_heap_watcher().pause_watching_this_thread() ));

        std::vector<ModuleFunctionOffset> tail_records;

        tail_records.reserve( MAX_CALLSTACK_RETAINED );

        //  First, determine how many entries we have in the trace.  Uninitialized
        //      entries will be nullptrs;

        int number_of_tail_entries = 0;

        while ((stack_tail_[number_of_tail_entries] != nullptr) && (number_of_tail_entries < MAX_CALLSTACK_RETAINED))
        {
            number_of_tail_entries++;
        }

        //  Get the symbols - this is an array of mangled strings.  If this fails,
        //      return the currently empty list

        char** backtrace_lines(backtrace_symbols(stack_tail_.data(), number_of_tail_entries));

        if (backtrace_lines == NULL)
        {
            perror("MemoryWatcher: Error getting symbols for stack tail");
            return tail_records;
        }

        for (int i = 0; i < number_of_tail_entries; i++)
        {
            tail_records.emplace_back(std::move(decode_mangled_line(backtrace_lines[i])));
        }

        free(backtrace_lines);

        return (tail_records);
    }

    ModuleFunctionOffset AllocationRecord::decode_mangled_line(const char* backtrace_line) const
    {
        //  First, get the backtrace line into a std::string

        std::string mangled_line(backtrace_line);

        //  Get the function or method name, but it has to be unmangled.

        std::string unmangled_name;

        std::string mangled_name =
            mangled_line.substr(mangled_line.find('(') + 1, mangled_line.find('+') - mangled_line.find('(') - 1);

        if (!mangled_name.empty())
        {
            constexpr size_t MANGLED_NAME_BUFFER_SIZE = 256;

            char* unmangling_buffer = static_cast<char*>(malloc(MANGLED_NAME_BUFFER_SIZE));
            size_t unmangling_buffer_size = MANGLED_NAME_BUFFER_SIZE;

            int status;

            //  We use the ABI for demangling.  If this fails, then we may have a pure C symbol
            //      so just pass through the original string.

            std::unique_ptr<char> unmangled_name_ptr(
                abi::__cxa_demangle(mangled_name.c_str(), unmangling_buffer, &unmangling_buffer_size, &status));

            if (status == 0)
            {
                unmangled_name = unmangled_name_ptr.get();
            }
            else
            {
                unmangled_name = mangled_name;
            }
        }

        //  Return the line

        return ModuleFunctionOffset(
            std::move(mangled_line.substr(0, mangled_line.find('('))), std::move(unmangled_name),
            std::move(mangled_line.substr(mangled_line.find('+'), mangled_line.find(')') - mangled_line.find('+'))));
    }

}  // namespace SEFUtils::MemoryWatcher
