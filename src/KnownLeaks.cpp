#include "HeapWatcher.hpp"

#include "BacktraceDemangler.hpp"


namespace SEFUtility::HeapWatcher
{
    const std::vector<ModuleFunctionOffset> KnownLeaks::symbols() const
    {
        return (std::move( symbols_for_addresses( known_leaks_ ) ));
    }
}