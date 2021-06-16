#pragma once

#include "HeapWatcher.hpp"

namespace SEFUtility::HeapWatcher
{
    ModuleFunctionOffset decode_mangled_line(const char* backtrace_line);

    std::vector<ModuleFunctionOffset> symbols_for_addresses(std::vector<const void*> addresses);

}  // namespace SEFUtility::HeapWatcher
