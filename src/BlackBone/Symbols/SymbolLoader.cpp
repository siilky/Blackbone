#include "SymbolLoader.h"
#include "SymbolCatalog.h"
#include "PDBHelper.h"
#include "../Misc/Trace.hpp"

#include <algorithm>
#include <iterator>

namespace blackbone
{

/// <summary>
/// Symbol of SymbolData with its sources
/// </summary>
struct SymbolField
{
    CatalogSymbol symbol;               // Catalog key
    const wchar_t* name;                // PDB name
    const wchar_t* altName;             // Alternative PDB name, may be null
    uint32_t SymbolData::* field32;     // Field of the 32-bit ntdll, may be null
    uint32_t SymbolData::* field64;     // Field of the 64-bit ntdll, may be null
};

static const SymbolField s_fields[] =
{
    { CatalogSymbol::LdrpHandleTlsData, L"LdrpHandleTlsData", nullptr, &SymbolData::LdrpHandleTlsData32, &SymbolData::LdrpHandleTlsData64 },
    // Recent ntdll PDBs use the plural name for the inverted function tables
    { CatalogSymbol::LdrpInvertedFunctionTable, L"LdrpInvertedFunctionTable", L"LdrpInvertedFunctionTables",
      &SymbolData::LdrpInvertedFunctionTable32, &SymbolData::LdrpInvertedFunctionTable64 },
    { CatalogSymbol::RtlInsertInvertedFunctionTable, L"RtlInsertInvertedFunctionTable", nullptr,
      &SymbolData::RtlInsertInvertedFunctionTable32, &SymbolData::RtlInsertInvertedFunctionTable64 },
    { CatalogSymbol::LdrpReleaseTlsEntry, L"LdrpReleaseTlsEntry", nullptr, &SymbolData::LdrpReleaseTlsEntry32, &SymbolData::LdrpReleaseTlsEntry64 },
    { CatalogSymbol::LdrProtectMrdata, L"LdrProtectMrdata", nullptr, &SymbolData::LdrProtectMrdata, nullptr },
};

/// <summary>
/// Field of the symbol in the ntdll of the given bitness
/// </summary>
/// <returns>Field; null if the bitness has no such symbol</returns>
static uint32_t SymbolData::* FieldOf( const SymbolField& symbol, const pe::PEImage& image )
{
    return image.mType() == mt_mod64 ? symbol.field64 : symbol.field32;
}

/// <summary>
/// Map a PDBHelper::Init failure to the loading status
/// </summary>
static NTSTATUS StatusFromPdb( HRESULT hr )
{
    if (hr == HRESULT_FROM_WIN32( ERROR_MOD_NOT_FOUND ))
        return STATUS_DLL_NOT_FOUND;

    if (hr == E_PDB_NOT_FOUND)
        return STATUS_NO_SUCH_FILE;

    BLACKBONE_TRACE( "PDB: loading failed with HRESULT 0x%08x", hr );
    return STATUS_UNSUCCESSFUL;
}

SymbolLoader::SymbolLoader( bool allowNetwork )
    : _allowNetwork( allowNetwork )
    , _x86OS( false )
    , _wow64Process( false )
{
    SYSTEM_INFO info = { };
    GetNativeSystemInfo( &info );

    if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
    {
        _x86OS = true;
    }
    else
    {
        BOOL wowSrc = FALSE;
        IsWow64Process( GetCurrentProcess(), &wowSrc );
        _wow64Process = wowSrc != 0;
    }
}

/// <summary>
/// Load the ntdll images from the disk unless an earlier step did, and record which files the RVAs refer to
/// </summary>
/// <param name="result">Receives the file header values of the images</param>
/// <returns>Status of reading the images</returns>
NTSTATUS SymbolLoader::LoadImages( SymbolData& result )
{
    // Images come from the disk: the symbols are looked up by file, not in a running process.
    // The 32-bit image loads last, so its view tells whether every image is loaded.
    if (!_ntdll32.base())
    {
        wchar_t buf[MAX_PATH] = { 0 };
        GetWindowsDirectoryW( buf, MAX_PATH );
        std::wstring windir( buf );

        NTSTATUS status = STATUS_SUCCESS;
        if (_x86OS)
        {
            status = _ntdll32.Load( std::wstring( windir + L"\\System32\\ntdll.dll" ), true );
        }
        else
        {
            // A WOW64 process reaches the native System32 through the Sysnative alias. Disabling the
            // redirection instead would also redirect the loading of msdia140.dll and its dependencies
            // when the 64-bit PDB is the first one opened.
            const wchar_t* system = _wow64Process ? L"\\Sysnative\\ntdll.dll" : L"\\System32\\ntdll.dll";

            status = _ntdll64.Load( windir + system, true );
            if (NT_SUCCESS( status ))
                status = _ntdll32.Load( std::wstring( windir + L"\\SysWOW64\\ntdll.dll" ), true );
        }

        // A view left by a failed parse would pass for a loaded image in the next step
        if (!NT_SUCCESS( status ))
        {
            _ntdll32.Release();
            return status;
        }
    }

    // GetSymbolAddress compares these with the target's ntdll before applying the RVAs
    auto timeDateStamp = []( const pe::PEImage& image )
    {
        const auto base = static_cast<const uint8_t*>(image.base());
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        return reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew)->FileHeader.TimeDateStamp;
    };

    result.TimeDateStamp32 = timeDateStamp( _ntdll32 );
    result.SizeOfImage32 = _ntdll32.imageSize();
    if (!_x86OS)
    {
        result.TimeDateStamp64 = timeDateStamp( _ntdll64 );
        result.SizeOfImage64 = _ntdll64.imageSize();
    }

    return STATUS_SUCCESS;
}

NTSTATUS SymbolLoader::Load( SymbolData& result )
{
    const NTSTATUS status = LoadCatalog( result );
    if (!NT_SUCCESS( status ))
        return status;

    return LoadPdb( result );
}

NTSTATUS SymbolLoader::LoadCatalog( SymbolData& result )
{
    const NTSTATUS status = LoadImages( result );

    // The x86 entries come from WOW64 builds; a native x86 ntdll is a different build, unchecked for false matches
    if (!NT_SUCCESS( status ) || _x86OS)
        return status;

    for (const pe::PEImage* image : { &_ntdll32, &_ntdll64 })
    {
        for (const auto& symbol : s_fields)
        {
            const auto field = FieldOf( symbol, *image );
            if (field && result.*field == 0)
                result.*field = FindInCatalog( *image, g_symbolCatalog, g_symbolCatalogSize, symbol.symbol );
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS SymbolLoader::LoadPdb( SymbolData& result )
{
    NTSTATUS status = LoadImages( result );
    if (!NT_SUCCESS( status ))
        return status;

    // A failed bitness does not prevent filling the other one; the first failure is reported
    for (const pe::PEImage* image : { &_ntdll32, &_ntdll64 })
    {
        if (!image->base())
            continue;

        const auto missing = [&]( const SymbolField& symbol )
        {
            const auto field = FieldOf( symbol, *image );
            return field && result.*field == 0;
        };

        if (std::none_of( std::begin( s_fields ), std::end( s_fields ), missing ))
            continue;

        PDBHelper pdb( _allowNetwork );
        const HRESULT hr = pdb.Init( image->path() );
        if (FAILED( hr ))
        {
            if (NT_SUCCESS( status ))
                status = StatusFromPdb( hr );

            continue;
        }

        for (const auto& symbol : s_fields)
        {
            if (!missing( symbol ))
                continue;

            uint32_t& value = result.*FieldOf( symbol, *image );
            if (FAILED( pdb.GetSymAddress( symbol.name, value ) ) && symbol.altName)
                pdb.GetSymAddress( symbol.altName, value );
        }
    }

    return status;
}

}
