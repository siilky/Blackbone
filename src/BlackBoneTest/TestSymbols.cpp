#include "Common.h"

#include <filesystem>
#include <optional>

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

    TEST_CLASS( Symbols )
    {
    public:
        TEST_METHOD( SymbolsMatch )
        {
            SymbolData data;
            AssertEx::NtSuccess( SymbolLoader( true ).Load( data ), L"PDB symbol loading failed" );

            AssertEx::IsNotZero( data.LdrpHandleTlsData32, L"LdrpHandleTlsData32 missing from PDB" );
            AssertEx::IsNotZero( data.LdrpHandleTlsData64, L"LdrpHandleTlsData64 missing from PDB" );
            AssertEx::IsNotZero( data.LdrpInvertedFunctionTable32, L"LdrpInvertedFunctionTable32 missing from PDB" );
            AssertEx::IsNotZero( data.LdrpInvertedFunctionTable64, L"LdrpInvertedFunctionTable64 missing from PDB" );
            AssertEx::IsNotZero( data.RtlInsertInvertedFunctionTable32, L"RtlInsertInvertedFunctionTable32 missing from PDB" );
            AssertEx::IsNotZero( data.RtlInsertInvertedFunctionTable64, L"RtlInsertInvertedFunctionTable64 missing from PDB" );
            AssertEx::IsNotZero( data.LdrpReleaseTlsEntry32, L"LdrpReleaseTlsEntry32 missing from PDB" );
            AssertEx::IsNotZero( data.LdrpReleaseTlsEntry64, L"LdrpReleaseTlsEntry64 missing from PDB" );
            AssertEx::IsNotZero( data.LdrProtectMrdata, L"LdrProtectMrdata missing from PDB" );
        }

        TEST_METHOD( LoadSymbolsFirstLoadWins )
        {
            // Module initialization already loaded the symbols with network allowed. A new load
            // would fail in the empty stores, so only the saved status can be a success.
            NTSTATUS fresh = STATUS_SUCCESS, saved = STATUS_SUCCESS;
            {
                EmptySymbolStores stores( L"BlackBoneFirstLoad" );

                SymbolData data;
                fresh = SymbolLoader( false ).Load( data );
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

                downloadStatus = SymbolLoader( true ).Load( downloaded );
                localStatus = SymbolLoader( false ).Load( local );
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

                status = SymbolLoader( false ).Load( data );
                untouched = stores.untouched();
            }

            AssertEx::AreEqual<NTSTATUS>( STATUS_NO_SUCH_FILE, status );
            AssertEx::IsTrue( untouched, L"Symbol server was used without network permission" );
        }
    };
}
