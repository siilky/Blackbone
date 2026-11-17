#include "PDBHelper.h"
#include "../Misc/Trace.hpp"
#include "../PE/PEImage.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <sstream>
#include <vector>

#define CHECK_SUCCESS(e)\
{ \
    HRESULT hresult = (e); \
    if(FAILED(hresult)) \
    { \
        BLACKBONE_TRACE( "PDB: %s: (%s) failed with HRESULT 0x%08x", __FUNCTION__, #e , hresult ); \
        return hresult; \
    } \
}

namespace blackbone
{

// CodeView debug record of a PDB 7.0 reference
struct CodeViewRsds
{
    DWORD signature;    // "RSDS" as little-endian 0x53445352
    GUID guid;
    DWORD age;
    char name[1];       // PDB path, zero-terminated
};

/// <summary>
/// Current value of _NT_SYMBOL_PATH; the Win32 block is read because DIA itself only sees the value the process started with
/// </summary>
/// <returns>Full value, empty if the variable is not set</returns>
static std::wstring SymbolPathEnv()
{
    const DWORD size = GetEnvironmentVariableW( L"_NT_SYMBOL_PATH", nullptr, 0 );
    if (size == 0)
        return std::wstring();

    std::wstring env( size, L'\0' );
    env.resize( GetEnvironmentVariableW( L"_NT_SYMBOL_PATH", env.data(), size ) );
    return env;
}

/// <summary>
/// Local symbol stores of the single-tier layout: the default store and the local elements of _NT_SYMBOL_PATH.
/// Plain directories and cache* elements are not searched.
/// </summary>
static std::vector<std::wstring> LocalSymbolStores()
{
    std::vector<std::wstring> stores;

    wchar_t temp[MAX_PATH] = { };
    if (GetTempPathW( _countof( temp ), temp ) != 0)
        stores.emplace_back( std::wstring( temp ) + L"symbols" );

    std::wistringstream elements( SymbolPathEnv() );
    for (std::wstring element; std::getline( elements, element, L';' );)
    {
        // srv*<store>*...  or  symsrv*<symsrv dll>*<store>*...
        size_t start = 0;
        if (_wcsnicmp( element.c_str(), L"srv*", 4 ) == 0)
        {
            start = 4;
        }
        else if (_wcsnicmp( element.c_str(), L"symsrv*", 7 ) == 0)
        {
            start = element.find( L'*', 7 );
            if (start == element.npos)
                continue;

            start++;
        }
        else
        {
            continue;
        }

        std::wistringstream tokens( element.substr( start ) );
        for (std::wstring token; std::getline( tokens, token, L'*' );)
        {
            if (!token.empty() && _wcsnicmp( token.c_str(), L"http://", 7 ) != 0 && _wcsnicmp( token.c_str(), L"https://", 8 ) != 0)
                stores.push_back( std::move( token ) );
        }
    }

    return stores;
}

PDBHelper::PDBHelper( bool allowNetwork )
    : _allowNetwork( allowNetwork )
{
    // RPC_E_CHANGED_MODE means the thread is already initialized in another mode and must not be uninitialized by us
    _coInitialized = SUCCEEDED( CoInitialize( nullptr ) );

    if (!_allowNetwork)
        return;

    // If _NT_SYMBOL_PATH exists, use it
    _sympath = SymbolPathEnv();
    if (_sympath.empty())
    {
        wchar_t path[MAX_PATH] = { };
        if (GetTempPathW( _countof( path ), path ) != 0)
        {
            std::wstringstream wss;
            wss << L"srv*" << path << L"symbols*https://msdl.microsoft.com/download/symbols";
            _sympath = wss.str();
        }
    }
}

PDBHelper::~PDBHelper()
{
    // Must be released before CoUninitialize
    _global.Reset();
    _session.Reset();
    _source.Reset();

    if (_coInitialized)
        CoUninitialize();
}

/// <summary>
/// Initialize symbols for target file
/// </summary>
/// <param name="file">Fully qualified path to target PE file</param>
/// <returns>Status code; HRESULT_FROM_WIN32( ERROR_MOD_NOT_FOUND ) if DIA cannot be created</returns>
HRESULT PDBHelper::Init( const std::wstring& file )
{
    if (FAILED( CoCreateDiaDataSource() ))
        return HRESULT_FROM_WIN32( ERROR_MOD_NOT_FOUND );

    CHECK_SUCCESS( _allowNetwork
        ? _source->loadDataForExe( file.c_str(), _sympath.empty() ? nullptr : _sympath.c_str(), nullptr )
        : LoadFromLocalStore( file ) );
    CHECK_SUCCESS( _source->openSession( &_session ) );
    CHECK_SUCCESS( _session->get_globalScope( &_global ) );

    return PopulateSymbols();
}

/// <summary>
/// Open the PDB of the file from a local symbol store without any symbol server or registry lookup.
/// DIA cannot do it through loadDataForExe: restricting the symbol server there also blocks local srv* stores,
/// and _NT_SYMBOL_PATH is consulted even for an explicit search path.
/// </summary>
/// <param name="file">Fully qualified path to target PE file</param>
/// <returns>Status code; E_PDB_NOT_FOUND if no store holds a matching PDB, otherwise the first failure to open a candidate</returns>
HRESULT PDBHelper::LoadFromLocalStore( const std::wstring& file )
{
    pe::PEImage image;
    if (const NTSTATUS status = image.Load( file, true ); !NT_SUCCESS( status ))
    {
        BLACKBONE_TRACE( "PDB: %s: failed to load image, status 0x%08x", __FUNCTION__, status );
        return HRESULT_FROM_NT( status );
    }

    const auto debugDir = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(image.DirectoryAddress( IMAGE_DIRECTORY_ENTRY_DEBUG ));
    const size_t count = image.DirectorySize( IMAGE_DIRECTORY_ENTRY_DEBUG ) / sizeof( IMAGE_DEBUG_DIRECTORY );

    const CodeViewRsds* rsds = nullptr;
    size_t nameSize = 0;
    for (size_t i = 0; debugDir && i < count && !rsds; i++)
    {
        if (debugDir[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW || debugDir[i].SizeOfData <= offsetof( CodeViewRsds, name ))
            continue;

        const auto record = reinterpret_cast<const CodeViewRsds*>(image.ResolveRVAToVA( debugDir[i].AddressOfRawData ));
        if (record && record->signature == 0x53445352)
        {
            rsds = record;
            nameSize = debugDir[i].SizeOfData - offsetof( CodeViewRsds, name );
        }
    }

    if (!rsds)
        return E_PDB_NO_DEBUG_INFO;

    // The record may hold a build machine path; stores are keyed by the bare file name
    const std::wstring pdbName = Utils::StripPath( Utils::AnsiToWstring( std::string( rsds->name, strnlen( rsds->name, nameSize ) ) ) );

    wchar_t id[64] = { };
    const GUID& g = rsds->guid;
    swprintf_s( id, L"%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%X", g.Data1, g.Data2, g.Data3,
                g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7], rsds->age );

    HRESULT failure = S_OK;
    for (const auto& store : LocalSymbolStores())
    {
        const std::wstring pdbPath = store + L"\\" + pdbName + L"\\" + id + L"\\" + pdbName;
        if (GetFileAttributesW( pdbPath.c_str() ) == INVALID_FILE_ATTRIBUTES)
            continue;

        // A rejected PDB leaves the data source reusable for the next candidate
        GUID guid = rsds->guid;
        const HRESULT hr = _source->loadAndValidateDataFromPdb( pdbPath.c_str(), &guid, 0, rsds->age );
        if (SUCCEEDED( hr ))
            return S_OK;

        // A different build under the same name is a miss; any other failure is reported if no candidate opens
        if (hr != E_PDB_INVALID_SIG && hr != E_PDB_INVALID_AGE && SUCCEEDED( failure ))
            failure = hr;
    }

    return SUCCEEDED( failure ) ? E_PDB_NOT_FOUND : failure;
}

/// <summary>
/// Get symbol RVA
/// </summary>
/// <param name="symName">Symbol name</param>
/// <param name="result">Resulting RVA</param>
/// <returns>Status code</returns>
HRESULT PDBHelper::GetSymAddress( const std::wstring& symName, uint32_t& result )
{
    auto found = _cache.find( symName );
    if (found != _cache.end())
    {
        result = found->second;
        return S_OK;
    }

    return E_PDB_DBG_NOT_FOUND;
}

/// <summary>
/// Initialize DIA
/// </summary>
/// <returns>Status code</returns>
HRESULT PDBHelper::CoCreateDiaDataSource()
{
    const auto pSource = reinterpret_cast<void**>(_source.ReleaseAndGetAddressOf());

    // Try to get from COM
    HRESULT hr = CoCreateInstance( CLSID_DiaSource, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IDiaDataSource), pSource );

    // Retry with direct export call
    if (hr == REGDB_E_CLASSNOTREG)
    {
        // Prefer the copy next to the module that holds this code over the DLL search order
        static const char anchor = 0;
        HMODULE hMod = nullptr;
        HMODULE self = nullptr;
        wchar_t modulePath[MAX_PATH] = { };
        if (GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&anchor), &self )
            && GetModuleFileNameW( self, modulePath, _countof( modulePath ) ) != 0)
        {
            hMod = LoadLibraryW( std::filesystem::path( modulePath ).replace_filename( L"msdia140.dll" ).c_str() );
        }

        if (!hMod)
            hMod = LoadLibraryW( L"msdia140.dll" );

        if (!hMod)
        {
            const auto err = GetLastError();
            BLACKBONE_TRACE( "PDB: Failed to load msdia140.dll, error 0x%08x", err );
            return HRESULT_FROM_WIN32( err );
        }

        auto DllGetClassObject = reinterpret_cast<BOOL( WINAPI* )(REFCLSID, REFIID, LPVOID)>(GetProcAddress( hMod, "DllGetClassObject" ));
        if (!DllGetClassObject)
        {
            const auto err = GetLastError();
            BLACKBONE_TRACE( "PDB: Failed to get DllGetClassObject from msdia140.dll" );
            return HRESULT_FROM_WIN32( err );
        }

        Microsoft::WRL::ComPtr<IClassFactory> classFactory;
        CHECK_SUCCESS( DllGetClassObject( CLSID_DiaSource, IID_IClassFactory, classFactory.GetAddressOf() ) );
        hr = classFactory->CreateInstance( nullptr, IID_IDiaDataSource, pSource );
    }

    if (FAILED( hr ))
    {
        BLACKBONE_TRACE( "PDB: %s failed with HRESULT 0x%08x", __FUNCTION__, hr );
    }

    return hr;
}

/// <summary>
/// Build module symbol map
/// </summary>
/// <returns>Status code</returns>
HRESULT PDBHelper::PopulateSymbols()
{
    Microsoft::WRL::ComPtr<IDiaEnumSymbols> symbols;
    CHECK_SUCCESS( _global->findChildren( SymTagNull, nullptr, nsNone, &symbols ) );

    ULONG count = 0;
    Microsoft::WRL::ComPtr<IDiaSymbol> symbol;
    while (SUCCEEDED( symbols->Next( 1, &symbol, &count ) ) && count != 0)
    {
        DWORD rva = 0;
        BSTR name = nullptr;

        symbol->get_relativeVirtualAddress( &rva );
        symbol->get_undecoratedName( &name );
        std::unique_ptr<OLECHAR, decltype(&SysFreeString)> nameGuard( name, &SysFreeString );

        if (name && rva)
        {
            std::wstring wname( name );

            // Remove x86 __stdcall decoration
            if (wname[0] == L'@' || wname[0] == L'_')
            {
                wname.erase( 0, 1 );
            }

            auto pos = wname.rfind( L'@' );
            if (pos != wname.npos)
            {
                wname.erase( pos );
            }

            _cache.emplace( std::move( wname ), rva );
        }

        symbol.Reset();
    }

    return S_OK;
}

}
