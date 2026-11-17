#pragma once
#include "../include/Types.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace blackbone
{

namespace pe { class PEImage; }

/// <summary>
/// Ntdll internal symbols covered by the fingerprint catalog
/// </summary>
enum class CatalogSymbol : uint8_t
{
    LdrpHandleTlsData,
    LdrpInvertedFunctionTable,          // Data: the fingerprint starts at an instruction that addresses it
    RtlInsertInvertedFunctionTable,
    LdrpReleaseTlsEntry,
    LdrProtectMrdata,                   // x86 only
};

/// <summary>
/// Fingerprint of one symbol in the ntdll builds it was generated from.
/// A code fingerprint starts at the function; a data fingerprint starts at an instruction
/// whose 32-bit operand addresses the symbol.
/// </summary>
struct CatalogEntry
{
    CatalogSymbol symbol;
    bool is64;                  // Bitness of the ntdll image
    uint16_t size;              // Fingerprint length in bytes
    const uint8_t* bytes;       // Fingerprint bytes, 0 where the mask skips
    const uint8_t* mask;        // Nonzero where the byte is compared
    uint16_t operandOffset;     // Data only: offset of the 32-bit address operand
    uint16_t operandEnd;        // Data only, x64: offset of the end of the instruction holding the operand (RIP base)
};

// Generated table, see SymbolCatalogData.cpp
extern const CatalogEntry g_symbolCatalog[];
extern const size_t g_symbolCatalogSize;

/// <summary>
/// Find the only place in the executable sections of the image where the masked bytes match
/// </summary>
/// <param name="image">Ntdll image</param>
/// <param name="bytes">Fingerprint bytes</param>
/// <param name="mask">Nonzero where the byte is compared</param>
/// <param name="size">Fingerprint length</param>
/// <returns>RVA of the match; empty if there is no match or more than one</returns>
std::optional<uint32_t> FindUnique( const pe::PEImage& image, const uint8_t* bytes, const uint8_t* mask, size_t size );

/// <summary>
/// Resolve a symbol by the catalog entries of the image bitness
/// </summary>
/// <param name="image">Ntdll image</param>
/// <param name="entries">Catalog entries</param>
/// <param name="count">Number of entries</param>
/// <param name="symbol">Symbol to resolve</param>
/// <returns>Symbol RVA; 0 if no entry matches uniquely or matching entries disagree</returns>
uint32_t FindInCatalog( const pe::PEImage& image, const CatalogEntry* entries, size_t count, CatalogSymbol symbol );

}
