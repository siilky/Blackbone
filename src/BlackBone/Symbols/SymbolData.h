#pragma once
#include "../include/Types.h"
#include "../Include/CallResult.h"

namespace blackbone
{

class ProcessModules;

/// <summary>
/// Ntdll internal symbols as RVAs in the ntdll image of the matching bitness; 0 if not resolved
/// </summary>
struct SymbolData
{
    NTSTATUS status = STATUS_SUCCESS;               // Loading status; does not reflect individual missing symbols
    uint32_t LdrpHandleTlsData32 = 0;               // LdrpHandleTlsData
    uint32_t LdrpHandleTlsData64 = 0;               // LdrpHandleTlsData
    uint32_t LdrpInvertedFunctionTable32 = 0;       // LdrpInvertedFunctionTable
    uint32_t LdrpInvertedFunctionTable64 = 0;       // LdrpInvertedFunctionTable
    uint32_t RtlInsertInvertedFunctionTable32 = 0;  // RtlInsertInvertedFunctionTable
    uint32_t RtlInsertInvertedFunctionTable64 = 0;  // RtlInsertInvertedFunctionTable
    uint32_t LdrpReleaseTlsEntry32 = 0;             // LdrpReleaseTlsEntry
    uint32_t LdrpReleaseTlsEntry64 = 0;             // LdrpReleaseTlsEntry
    uint32_t LdrProtectMrdata = 0;                  // LdrProtectMrdata, x86 only
};

/// <summary>
/// Load ntdll symbols for the whole process. Only the first load takes effect, whether it
/// comes from this call or from a consumer that needs a symbol (which loads without network);
/// later calls return the saved status without loading again.
/// </summary>
/// <param name="allowNetwork">Allow downloading PDBs from the symbol server</param>
/// <returns>Loading status</returns>
BLACKBONE_API NTSTATUS LoadSymbols( bool allowNetwork = false );

/// <summary>
/// Ntdll internal symbols, loaded without network on the first call unless LoadSymbols ran first
/// </summary>
/// <returns>Loaded symbols</returns>
const SymbolData& GetSymbols();

/// <summary>
/// Address of an ntdll internal symbol in the target process
/// </summary>
/// <param name="modules">Target process modules</param>
/// <param name="rva">Symbol RVA from GetSymbols()</param>
/// <param name="type">Bitness of the ntdll that holds the symbol</param>
/// <returns>Symbol address; the loading status if the symbol is missing because loading failed,
/// STATUS_NOT_FOUND if it is missing otherwise or the target has no such ntdll</returns>
call_result_t<ptr_t> GetSymbolAddress( ProcessModules& modules, uint32_t rva, eModType type );

}
