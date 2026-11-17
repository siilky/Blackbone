#include "SymbolData.h"
#include "SymbolLoader.h"
#include "../Process/Process.h"

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

call_result_t<ptr_t> GetSymbolAddress( Process& process, uint32_t rva, eModType type )
{
    const SymbolData& symbols = GetSymbols();
    if (rva == 0)
        return NT_SUCCESS( symbols.status ) ? STATUS_NOT_FOUND : symbols.status;

    auto ntdll = process.modules().GetModule( L"ntdll.dll", LdrList, type );
    if (!ntdll)
        return STATUS_NOT_FOUND;

    // RVAs come from the ntdll file on disk, which may differ from the target's ntdll, for example
    // after an update that the running process predates. The file header precedes the optional
    // header, so the TimeDateStamp offset is the same for PE32 and PE32+.
    auto lfanew = process.memory().Read<LONG>( ntdll->baseAddress + offsetof( IMAGE_DOS_HEADER, e_lfanew ) );
    if (!lfanew)
        return lfanew.status;

    auto timeDateStamp = process.memory().Read<DWORD>(
        ntdll->baseAddress + lfanew.result() + offsetof( IMAGE_NT_HEADERS32, FileHeader ) + offsetof( IMAGE_FILE_HEADER, TimeDateStamp ) );
    if (!timeDateStamp)
        return timeDateStamp.status;

    const bool is64 = type == mt_mod64;
    if (timeDateStamp.result() != (is64 ? symbols.TimeDateStamp64 : symbols.TimeDateStamp32)
        || ntdll->size != (is64 ? symbols.SizeOfImage64 : symbols.SizeOfImage32))
    {
        return STATUS_IMAGE_CHECKSUM_MISMATCH;
    }

    return ntdll->baseAddress + rva;
}

}
