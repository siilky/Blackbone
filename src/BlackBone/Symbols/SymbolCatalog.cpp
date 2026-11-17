#include "SymbolCatalog.h"
#include "../PE/PEImage.h"

#include <algorithm>
#include <cstring>

namespace blackbone
{

std::optional<uint32_t> FindUnique( const pe::PEImage& image, const uint8_t* bytes, const uint8_t* mask, size_t size )
{
    std::optional<uint32_t> found;
    if (size == 0)
        return found;

    // Uniqueness spans all executable sections: a second match anywhere makes the fingerprint ambiguous
    for (const auto& section : image.sections())
    {
        if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE))
            continue;

        const auto data = reinterpret_cast<const uint8_t*>(image.ResolveRVAToVA( section.VirtualAddress ));
        const size_t length = std::min<size_t>( section.Misc.VirtualSize, section.SizeOfRawData );
        if (!data || length < size)
            continue;

        for (size_t offset = 0; offset <= length - size; offset++)
        {
            size_t i = 0;
            while (i < size && (!mask[i] || data[offset + i] == bytes[i]))
                i++;

            if (i != size)
                continue;

            if (found)
                return std::nullopt;

            found = static_cast<uint32_t>(section.VirtualAddress + offset);
        }
    }

    return found;
}

uint32_t FindInCatalog( const pe::PEImage& image, const CatalogEntry* entries, size_t count, CatalogSymbol symbol )
{
    const bool is64 = image.mType() == mt_mod64;

    uint32_t result = 0;
    for (size_t i = 0; i < count; i++)
    {
        const auto& entry = entries[i];
        if (entry.symbol != symbol || entry.is64 != is64)
            continue;

        const auto match = FindUnique( image, entry.bytes, entry.mask, entry.size );
        if (!match)
            continue;

        uint32_t rva = *match;
        if (symbol == CatalogSymbol::LdrpInvertedFunctionTable)
        {
            uint32_t operand = 0;
            memcpy( &operand, reinterpret_cast<const void*>(image.ResolveRVAToVA( rva + entry.operandOffset )), sizeof( operand ) );

            // x64 addresses the data relative to the next instruction; x86 holds an absolute address
            // based on the image base from the headers of the same view
            rva = is64
                ? rva + entry.operandEnd + static_cast<int32_t>(operand)
                : operand - static_cast<uint32_t>(image.imageBase());
        }

        // Entries from different epochs may both match only if they agree
        if (result != 0 && result != rva)
            return 0;

        result = rva;
    }

    return result;
}

}
