#include "Common.h"
#include <BlackBone/Symbols/SymbolCatalog.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <vector>

namespace Testing
{
    namespace fs = std::filesystem;

    /// <summary>
    /// Environment variable override restored on scope exit
    /// </summary>
    class ScopedEnv
    {
    public:
        ScopedEnv( const wchar_t* name, const std::wstring& value )
            : _name( name )
        {
            if (const DWORD size = GetEnvironmentVariableW( name, nullptr, 0 ); size != 0)
            {
                _saved.emplace( size, L'\0' );
                GetEnvironmentVariableW( name, _saved->data(), size );
                _saved->resize( size - 1 );
            }

            SetEnvironmentVariableW( name, value.c_str() );
        }

        ~ScopedEnv()
        {
            SetEnvironmentVariableW( _name, _saved ? _saved->c_str() : nullptr );
        }

    private:
        const wchar_t* _name;
        std::optional<std::wstring> _saved;
    };

    /// <summary>
    /// Makes both local store locations empty: the default store (%TEMP%\symbols) and the store of
    /// _NT_SYMBOL_PATH, so only the symbol server could provide the PDBs. The environment is changed
    /// through the Win32 block, which the loader reads; DIA itself sees only the environment the process
    /// started with, so only an explicit search path passed to DIA can carry it.
    /// </summary>
    class EmptySymbolStores
    {
    public:
        explicit EmptySymbolStores( const wchar_t* name )
            : _root( fs::temp_directory_path() / name )
        {
            fs::remove_all( _root );
            fs::create_directories( _root );

            _tmp.emplace( L"TMP", _root.wstring() );
            _temp.emplace( L"TEMP", _root.wstring() );
            _symbolPath.emplace( L"_NT_SYMBOL_PATH", L"srv*" + (_root / L"store").wstring() + L"*https://msdl.microsoft.com/download/symbols" );
        }

        ~EmptySymbolStores()
        {
            _symbolPath.reset();
            _temp.reset();
            _tmp.reset();
            fs::remove_all( _root );
        }

        bool untouched() const { return fs::is_empty( _root ); }

    private:
        fs::path _root;
        std::optional<ScopedEnv> _tmp, _temp, _symbolPath;
    };

    const std::pair<uint32_t SymbolData::*, const wchar_t*> SymbolFields[] =
    {
        { &SymbolData::LdrpHandleTlsData32, L"LdrpHandleTlsData32" },
        { &SymbolData::LdrpHandleTlsData64, L"LdrpHandleTlsData64" },
        { &SymbolData::LdrpInvertedFunctionTable32, L"LdrpInvertedFunctionTable32" },
        { &SymbolData::LdrpInvertedFunctionTable64, L"LdrpInvertedFunctionTable64" },
        { &SymbolData::RtlInsertInvertedFunctionTable32, L"RtlInsertInvertedFunctionTable32" },
        { &SymbolData::RtlInsertInvertedFunctionTable64, L"RtlInsertInvertedFunctionTable64" },
        { &SymbolData::LdrpReleaseTlsEntry32, L"LdrpReleaseTlsEntry32" },
        { &SymbolData::LdrpReleaseTlsEntry64, L"LdrpReleaseTlsEntry64" },
        { &SymbolData::LdrProtectMrdata, L"LdrProtectMrdata" },
    };

    TEST_CLASS( Symbols )
    {
    public:
        TEST_METHOD( SymbolsMatch )
        {
            SymbolData pdb, catalog;
            AssertEx::NtSuccess( SymbolLoader( true ).LoadPdb( pdb ), L"PDB symbol loading failed" );
            AssertEx::NtSuccess( SymbolLoader( false ).LoadCatalog( catalog ), L"Catalog loading failed" );

            for (const auto& [field, name] : SymbolFields)
            {
                AssertEx::IsNotZero( pdb.*field, (std::wstring( name ) + L" missing from PDB").c_str() );
                AssertEx::IsNotZero( catalog.*field, (std::wstring( name ) + L" missing from catalog").c_str() );
                AssertEx::AreEqual( pdb.*field, catalog.*field, name );
            }
        }

        TEST_METHOD( CatalogWithoutPdb )
        {
            // A PDB is opened only for missing fields, and any attempt fails in the empty stores
            SymbolData data;
            NTSTATUS status = STATUS_UNSUCCESSFUL;
            bool untouched = false;
            {
                EmptySymbolStores stores( L"BlackBoneCatalogOnly" );

                status = SymbolLoader( false ).Load( data );
                untouched = stores.untouched();
            }

            AssertEx::AreEqual<NTSTATUS>( STATUS_SUCCESS, status );
            AssertEx::IsTrue( untouched, L"Symbol stores were used" );

            for (const auto& [field, name] : SymbolFields)
                AssertEx::IsNotZero( data.*field, name );
        }

        TEST_METHOD( LoadSymbolsFirstLoadWins )
        {
            // Module initialization already loaded the symbols with network allowed. A new load
            // would fail in the empty stores, so only the saved status can be a success.
            NTSTATUS fresh = STATUS_SUCCESS, saved = STATUS_SUCCESS;
            {
                EmptySymbolStores stores( L"BlackBoneFirstLoad" );

                SymbolData data;
                fresh = SymbolLoader( false ).LoadPdb( data );
                saved = LoadSymbols( false );
            }

            AssertEx::AreEqual<NTSTATUS>( STATUS_NO_SUCH_FILE, fresh, L"The stores are not empty" );
            AssertEx::AreEqual<NTSTATUS>( STATUS_SUCCESS, saved );
        }

        TEST_METHOD( LocalStoreWithoutNetwork )
        {
            // The networked load fills the store named by the current _NT_SYMBOL_PATH, the load
            // without network must read it back
            SymbolData downloaded, local;
            NTSTATUS downloadStatus = STATUS_SUCCESS, localStatus = STATUS_SUCCESS;
            {
                EmptySymbolStores stores( L"BlackBoneLocalStore" );

                downloadStatus = SymbolLoader( true ).LoadPdb( downloaded );
                localStatus = SymbolLoader( false ).LoadPdb( local );
            }

            AssertEx::NtSuccess( downloadStatus, L"PDB symbol loading failed" );
            AssertEx::NtSuccess( localStatus, L"Local store was not used" );

            AssertEx::AreEqual( downloaded.LdrpHandleTlsData32, local.LdrpHandleTlsData32, L"LdrpHandleTlsData32" );
            AssertEx::AreEqual( downloaded.LdrpHandleTlsData64, local.LdrpHandleTlsData64, L"LdrpHandleTlsData64" );
            AssertEx::AreEqual( downloaded.LdrpInvertedFunctionTable32, local.LdrpInvertedFunctionTable32, L"LdrpInvertedFunctionTable32" );
            AssertEx::AreEqual( downloaded.LdrpInvertedFunctionTable64, local.LdrpInvertedFunctionTable64, L"LdrpInvertedFunctionTable64" );
            AssertEx::AreEqual( downloaded.RtlInsertInvertedFunctionTable32, local.RtlInsertInvertedFunctionTable32, L"RtlInsertInvertedFunctionTable32" );
            AssertEx::AreEqual( downloaded.RtlInsertInvertedFunctionTable64, local.RtlInsertInvertedFunctionTable64, L"RtlInsertInvertedFunctionTable64" );
            AssertEx::AreEqual( downloaded.LdrpReleaseTlsEntry32, local.LdrpReleaseTlsEntry32, L"LdrpReleaseTlsEntry32" );
            AssertEx::AreEqual( downloaded.LdrpReleaseTlsEntry64, local.LdrpReleaseTlsEntry64, L"LdrpReleaseTlsEntry64" );
            AssertEx::AreEqual( downloaded.LdrProtectMrdata, local.LdrProtectMrdata, L"LdrProtectMrdata" );
            AssertEx::IsNotZero( local.LdrpHandleTlsData64, L"LdrpHandleTlsData64 missing from local PDB" );
        }

        TEST_METHOD( NoNetwork )
        {
            SymbolData data;
            NTSTATUS status = STATUS_SUCCESS;
            bool untouched = false;
            {
                EmptySymbolStores stores( L"BlackBoneNoNetwork" );

                status = SymbolLoader( false ).LoadPdb( data );
                untouched = stores.untouched();
            }

            AssertEx::AreEqual<NTSTATUS>( STATUS_NO_SUCH_FILE, status );
            AssertEx::IsTrue( untouched, L"Symbol server was used without network permission" );
        }
    };

    // FindInCatalog and the catalog table are not exported from the dll preset
#ifndef BLACKBONE_IMPORTS
    TEST_CLASS( SymbolsCatalogLookup )
    {
    public:
        TEST_CLASS_INITIALIZE( Init )
        {
            // A WOW64 host reads SysWOW64 through System32, so this is the host-bitness ntdll
            wchar_t dir[MAX_PATH] = { };
            GetSystemDirectoryW( dir, MAX_PATH );
            AssertEx::NtSuccess( s_ntdll.Load( std::wstring( dir ) + L"\\ntdll.dll", true ) );

            SymbolData pdb;
            AssertEx::NtSuccess( SymbolLoader( true ).LoadPdb( pdb ), L"PDB symbol loading failed" );

            s_tlsRva = Is64 ? pdb.LdrpHandleTlsData64 : pdb.LdrpHandleTlsData32;
            s_insertRva = Is64 ? pdb.RtlInsertInvertedFunctionTable64 : pdb.RtlInsertInvertedFunctionTable32;
        }

        TEST_CLASS_CLEANUP( Cleanup )
        {
            s_ntdll.Release();
        }

        TEST_METHOD( CatalogResolvesToPdb )
        {
            AssertEx::AreEqual( s_tlsRva, FindInCatalog( s_ntdll, g_symbolCatalog, g_symbolCatalogSize, CatalogSymbol::LdrpHandleTlsData ) );
        }

        TEST_METHOD( NonUniqueFingerprint )
        {
            // The prefix still matches at the function start, among other places
            CatalogEntry entry = Matching( CatalogSymbol::LdrpHandleTlsData, s_tlsRva );
            entry.size = 2;

            AssertEx::IsZero( FindInCatalog( s_ntdll, &entry, 1, CatalogSymbol::LdrpHandleTlsData ) );
        }

        TEST_METHOD( OtherBitness )
        {
            CatalogEntry entry = Matching( CatalogSymbol::LdrpHandleTlsData, s_tlsRva );
            entry.is64 = !entry.is64;

            AssertEx::IsZero( FindInCatalog( s_ntdll, &entry, 1, CatalogSymbol::LdrpHandleTlsData ) );
        }

        TEST_METHOD( AlteredByte )
        {
            CatalogEntry entry = Matching( CatalogSymbol::LdrpHandleTlsData, s_tlsRva );

            std::vector<uint8_t> bytes( entry.bytes, entry.bytes + entry.size );
            const auto compared = std::find_if( entry.mask, entry.mask + entry.size, []( uint8_t m ) { return m != 0; } );
            bytes[compared - entry.mask] ^= 0xFF;
            entry.bytes = bytes.data();

            AssertEx::IsZero( FindInCatalog( s_ntdll, &entry, 1, CatalogSymbol::LdrpHandleTlsData ) );
        }

        TEST_METHOD( ConflictingEntries )
        {
            // Each entry matches uniquely on its own, but at different functions
            CatalogEntry entries[] =
            {
                Matching( CatalogSymbol::LdrpHandleTlsData, s_tlsRva ),
                Matching( CatalogSymbol::RtlInsertInvertedFunctionTable, s_insertRva ),
            };
            entries[1].symbol = CatalogSymbol::LdrpHandleTlsData;

            AssertEx::IsZero( FindInCatalog( s_ntdll, entries, _countof( entries ), CatalogSymbol::LdrpHandleTlsData ) );
        }

        TEST_METHOD( SymbolWithoutEntries )
        {
            const CatalogEntry entry = Matching( CatalogSymbol::RtlInsertInvertedFunctionTable, s_insertRva );

            AssertEx::IsZero( FindInCatalog( s_ntdll, &entry, 1, CatalogSymbol::LdrpHandleTlsData ) );
        }

    private:
        /// <summary>
        /// Catalog entry that alone resolves the symbol in the installed ntdll
        /// </summary>
        static CatalogEntry Matching( CatalogSymbol symbol, uint32_t rva )
        {
            for (size_t i = 0; i < g_symbolCatalogSize; i++)
            {
                const auto& entry = g_symbolCatalog[i];
                if (entry.symbol == symbol && entry.is64 == Is64 && FindInCatalog( s_ntdll, &entry, 1, symbol ) == rva)
                    return entry;
            }

            Assert::Fail( L"No catalog entry resolves the symbol in the installed ntdll" );
            return { };
        }

        static constexpr bool Is64 = sizeof( void* ) == sizeof( uint64_t );

        inline static pe::PEImage s_ntdll;
        inline static uint32_t s_tlsRva = 0, s_insertRva = 0;
    };
#endif
}
