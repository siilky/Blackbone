#include "SymbolData.h"
#include "SymbolLoader.h"

namespace blackbone
{

const SymbolData& GetSymbols()
{
    // Loading may read PDBs, so it runs only when a consumer needs a field, never from
    // InitializeOnce. The magic static makes concurrent callers wait for the complete data;
    // a load that returns leaves unresolved fields 0 for good, and consumers handle them.
    // An exception from Load leaves the static uninitialized, so the next call retries.
    // Pattern selection reads the OS version, which InitializeOnce fills in the Process
    // constructor; every consumer is part of a Process.
    static const SymbolData symbols = []
    {
        SymbolData result;
        SymbolLoader().Load( result );
        return result;
    }();

    return symbols;
}

}
