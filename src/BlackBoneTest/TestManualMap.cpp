#include "Common.h"

namespace Testing
{
    struct DllLoadData
    {
        bool initialized = false;
        bool deinitialized = false;
        bool static_initialized = false;
        bool static_thread_initialized = false;
        bool static_func_initialized = false;
        bool seh_internal = false;
        bool seh_external = false;
        bool ceh_internal = false;
        bool ceh_external = false;
        bool ceh_uncaught = false;
    };

    TEST_CLASS( ManualMapping )
    {
    public:
        TEST_CLASS_INITIALIZE( Initialize )
        {
            nativeMods.emplace( L"combase.dll" );
            nativeMods.emplace( L"user32.dll" );

            modList.emplace( L"windows.storage.dll" );
            modList.emplace( L"shell32.dll" );
            modList.emplace( L"shlwapi.dll" );
        }

        TEST_METHOD_CLEANUP( Cleanup )
        {
            _proc.Terminate();
        }

        static LoadData MapCallback( CallbackType type, void* /*context*/, Process& /*process*/, const ModuleData& modInfo )
        {
            if (type == PreCallback)
            {
                if (nativeMods.count( modInfo.name ))
                    return LoadData( MT_Native, Ldr_None );
            }
            else
            {
                if (modList.count( modInfo.name ))
                    return LoadData( MT_Default, Ldr_ModList );
            }

            return LoadData( MT_Default, Ldr_None );
        };

        TEST_METHOD( FromFile32 )
        {
            MapFromFile( GetTestHelperHost32(), GetTestHelperDll32() );
        }

        TEST_METHOD( FromFile64 )
        {
            MapFromFile( GetTestHelperHost64(), GetTestHelperDll64() );
        }
 
        TEST_METHOD( FromMemory32 )
        {
            MapFromMemory( GetTestHelperHost32(), GetTestHelperDll32() );
        }

        TEST_METHOD( FromMemory64 )
        {
            MapFromMemory( GetTestHelperHost64(), GetTestHelperDll64() );
        }

    private:
        void MapFromFile( const std::wstring& hostPath, const std::wstring& dllPath )
        {
            Process& proc = _proc;
            // Initialize through a separate thread while the main thread stays suspended.
            NTSTATUS status = proc.CreateAndAttach( hostPath, true, false );
            AssertEx::NtSuccess( status );
            AssertEx::NtSuccess( proc.EnsureInit() );

            auto image = proc.mmap().MapImage( dllPath, ManualImports, &MapCallback );
            AssertEx::NtSuccess( image.status );
            AssertEx::IsNotNull( image.result().get(), L"Mapped image" );

            auto g_loadDataPtr = proc.modules().GetExport( image.result(), "g_LoadData" );
            AssertEx::NtSuccess( g_loadDataPtr.status );
            AssertEx::IsNotZero( g_loadDataPtr->procAddress, L"g_LoadData export" );

            auto g_loadData = proc.memory().Read<DllLoadData>( g_loadDataPtr->procAddress );
            AssertEx::NtSuccess( g_loadData.status );

            ValidateDllLoad( g_loadData.result() );
        }

        void MapFromMemory( const std::wstring& hostPath, const std::wstring& dllPath )
        {
            auto[buf, size] = GetFileData( dllPath );
            AssertEx::IsNotZero( size );

            Process& proc = _proc;
            NTSTATUS status = proc.CreateAndAttach( hostPath, true, false );
            AssertEx::NtSuccess( status );
            AssertEx::NtSuccess( proc.EnsureInit() );

            auto image = proc.mmap().MapImage( size, buf.get(), false, ManualImports, &MapCallback );
            AssertEx::NtSuccess( image.status );
            AssertEx::IsNotNull( image.result().get(), L"Mapped image" );

            auto g_loadDataPtr = proc.modules().GetExport( image.result(), "g_LoadData" );
            AssertEx::NtSuccess( g_loadDataPtr.status );
            AssertEx::IsNotZero( g_loadDataPtr->procAddress, L"g_LoadData export" );

            auto g_loadData = proc.memory().Read<DllLoadData>( g_loadDataPtr->procAddress );
            AssertEx::NtSuccess( g_loadData.status );

            ValidateDllLoad( g_loadData.result() );
        }

        void ValidateDllLoad( const DllLoadData& data )
        {
            AssertEx::IsTrue( data.initialized, L"initialized" );
            AssertEx::IsFalse( data.deinitialized, L"deinitialized" );
            AssertEx::IsTrue( data.static_initialized, L"static_initialized" );
            AssertEx::IsTrue( data.static_thread_initialized, L"static_thread_initialized" );
            AssertEx::IsTrue( data.static_func_initialized, L"static_func_initialized" );
            AssertEx::IsFalse( data.seh_internal, L"seh_internal" );
            AssertEx::IsTrue( data.seh_external, L"seh_external" );
            AssertEx::IsFalse( data.ceh_internal, L"ceh_internal" );
            AssertEx::IsTrue( data.ceh_external, L"ceh_external" );
            AssertEx::IsFalse( data.ceh_uncaught, L"ceh_uncaught" );
        }

        std::pair<std::unique_ptr<uint8_t[]>, uint32_t> GetFileData( const std::wstring& path )
        {
            auto hFile = Handle( CreateFileW( path.c_str(), FILE_GENERIC_READ, 0x7, nullptr, OPEN_EXISTING, 0, nullptr ) );
            if (hFile)
            {
                uint32_t size = GetFileSize( hFile, nullptr );
                auto buf = std::make_unique<uint8_t[]>( size );

                DWORD bytes = 0;
                if (ReadFile( hFile, buf.get(), size, &bytes, nullptr ))
                    return std::make_pair( std::move( buf ), size );
            }

            return std::pair<std::unique_ptr<uint8_t[]>, uint32_t>();
        }

    private:
        Process _proc;
        static inline std::set<std::wstring> nativeMods, modList;
    };
}
