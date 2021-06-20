#include "BacktraceDemangler.hpp"

#include <cxxabi.h>
#include <execinfo.h>

namespace SEFUtility::HeapWatcher
{
    ModuleFunctionOffset decode_mangled_line(const char* backtrace_line)
    {
        //  First, get the backtrace line into a std::string

        std::string mangled_line(backtrace_line);

        //  Get the function or method name, but it has to be unmangled.
        //
        //  The backtrace format is xxxxxxxxxxxxx(yyyyyyyyy+0xzzzz) [0xaaaaaaaa]
        //      where:
        //          x - module name
        //          y - function name
        //          z - offset
        //          a - address

        std::string unmangled_name;

        std::size_t open_paren_loc = mangled_line.find('(');
        std::size_t plus_loc = mangled_line.find('+', open_paren_loc);
        std::size_t close_paren_loc = mangled_line.find(')', open_paren_loc);

        std::string mangled_name = mangled_line.substr(open_paren_loc + 1, plus_loc - open_paren_loc - 1);

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

        return ModuleFunctionOffset(std::move(mangled_line.substr(mangled_line.find('[') + 1,
                                                                  mangled_line.find(']') - mangled_line.find('[') - 1)),
                                    std::move(mangled_line.substr(0, mangled_line.find('('))),
                                    std::move(unmangled_name),
                                    std::move(mangled_line.substr(plus_loc, close_paren_loc - plus_loc)));
    }

    std::vector<ModuleFunctionOffset> symbols_for_addresses(std::vector<const void*> addresses)
    {
        std::vector<ModuleFunctionOffset> tail_records;

        tail_records.reserve(addresses.size() + 1);

        //  Determine the number of addresses to demangle, this could be less than the
        //      size of the array.

        int num_addresses = 0;

        for( ; num_addresses < addresses.size(); num_addresses++ )
        {
            if( addresses.data()[num_addresses] == nullptr )
            {
                break;
            }
        }

        //  Get the symbols - this is an array of mangled strings.  If this fails,
        //      return the currently empty list.

        char** backtrace_lines(backtrace_symbols(const_cast<void* const*>(addresses.data()), num_addresses));

        if (backtrace_lines == NULL)
        {
            perror("MemoryWatcher: Error getting symbols for stack tail");
            return tail_records;
        }

        for (int i = 0; i < num_addresses; i++)
        {
            tail_records.emplace_back(std::move(decode_mangled_line(backtrace_lines[i])));
        }

        free(backtrace_lines);

        return (tail_records);
    }

}  // namespace SEFUtility::HeapWatcher
