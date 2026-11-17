#include "SymbolLoader.h"
#include "PDBHelper.h"
#include "../Misc/Trace.hpp"
#include "../Misc/Utils.h"
#include "../PE/PEImage.h"

namespace blackbone
{

/// <summary>
/// Load the symbols of one ntdll bitness
/// </summary>
/// <param name="pdb">Initialized PDB of the ntdll image</param>
static void LoadNtdllSymbols( PDBHelper& pdb, uint32_t& handleTlsData, uint32_t& invertedFunctionTable,
                              uint32_t& insertInvertedFunctionTable, uint32_t& releaseTlsEntry )
{
    pdb.GetSymAddress( L"LdrpHandleTlsData", handleTlsData );

    // Recent ntdll PDBs use the plural name for the inverted function tables.
    if (FAILED( pdb.GetSymAddress( L"LdrpInvertedFunctionTable", invertedFunctionTable ) ))
        pdb.GetSymAddress( L"LdrpInvertedFunctionTables", invertedFunctionTable );

    pdb.GetSymAddress( L"RtlInsertInvertedFunctionTable", insertInvertedFunctionTable );
    pdb.GetSymAddress( L"LdrpReleaseTlsEntry", releaseTlsEntry );
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

NTSTATUS SymbolLoader::Load( SymbolData& result )
{
    // Images come from the disk: the symbols are looked up by file, not in a running process
    pe::PEImage ntdll32, ntdll64;

    wchar_t buf[MAX_PATH] = { 0 };
    GetWindowsDirectoryW( buf, MAX_PATH );
    std::wstring windir( buf );

    NTSTATUS status = STATUS_SUCCESS;
    if (_x86OS)
    {
        status = ntdll32.Load( std::wstring( windir + L"\\System32\\ntdll.dll" ), true );
    }
    else
    {
        FsRedirector fsr( _wow64Process );

        status = ntdll64.Load( std::wstring( windir + L"\\System32\\ntdll.dll" ), true );
        if (NT_SUCCESS( status ))
            status = ntdll32.Load( std::wstring( windir + L"\\SysWOW64\\ntdll.dll" ), true );
    }

    if (!NT_SUCCESS( status ))
        return status;

    // A failed architecture does not prevent filling the other one; the first failure is reported
    PDBHelper sym32( _allowNetwork );
    HRESULT hr = sym32.Init( ntdll32.path() );
    if (SUCCEEDED( hr ))
    {
        LoadNtdllSymbols( sym32, result.LdrpHandleTlsData32, result.LdrpInvertedFunctionTable32,
                          result.RtlInsertInvertedFunctionTable32, result.LdrpReleaseTlsEntry32 );
        sym32.GetSymAddress( L"LdrProtectMrdata", result.LdrProtectMrdata );
    }
    else
    {
        status = StatusFromPdb( hr );
    }

    if (!_x86OS)
    {
        FsRedirector fsr( _wow64Process );

        PDBHelper sym64( _allowNetwork );
        hr = sym64.Init( ntdll64.path() );
        if (SUCCEEDED( hr ))
        {
            LoadNtdllSymbols( sym64, result.LdrpHandleTlsData64, result.LdrpInvertedFunctionTable64,
                              result.RtlInsertInvertedFunctionTable64, result.LdrpReleaseTlsEntry64 );
        }
        else if (NT_SUCCESS( status ))
        {
            status = StatusFromPdb( hr );
        }
    }

    return status;
}

}
