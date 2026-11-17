#include "SymbolData.h"
#include "SymbolLoader.h"
#include "../Process/ProcessModules.h"

namespace blackbone
{

/// <summary>
/// Single process-wide load shared by LoadSymbols and GetSymbols
/// </summary>
/// <param name="allowNetwork">Network permission, effective only for the call that loads</param>
/// <returns>Loaded symbols</returns>
static const SymbolData& LoadOnce( bool allowNetwork )
{
    // Loading may read PDBs, so it runs only when a consumer needs a field or the user asks
    // for it, never from InitializeOnce. The magic static makes concurrent callers wait for
    // the complete data, and the first caller's network permission decides for the process.
    // A load that returns is final, failed or not; an exception from Load leaves the static
    // uninitialized, so the next call retries.
    static const SymbolData symbols = [allowNetwork]
    {
        SymbolData result;
        result.status = SymbolLoader( allowNetwork ).Load( result );
        return result;
    }();

    return symbols;
}

NTSTATUS LoadSymbols( bool allowNetwork /*= false*/ )
{
    return LoadOnce( allowNetwork ).status;
}

const SymbolData& GetSymbols()
{
    return LoadOnce( false );
}

call_result_t<ptr_t> GetSymbolAddress( ProcessModules& modules, uint32_t rva, eModType type )
{
    if (rva == 0)
    {
        const NTSTATUS status = GetSymbols().status;
        return NT_SUCCESS( status ) ? STATUS_NOT_FOUND : status;
    }

    // RVAs come from the ntdll file on disk; the target's ntdll of the same bitness
    // is assumed to be the same file.
    auto ntdll = modules.GetModule( L"ntdll.dll", LdrList, type );
    if (!ntdll)
        return STATUS_NOT_FOUND;

    return ntdll->baseAddress + rva;
}

}
