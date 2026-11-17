#include "Common.h"
#include <3rd_party/VersionApi.h>

namespace Microsoft::VisualStudio::CppUnitTestFramework {
    template<> inline std::wstring ToString<HWND__>( HWND__* t ) { RETURN_WIDE_STRING( t ); }
}

namespace Testing
{
    TEST_CLASS( Syscall )
    {
    public:
        TEST_METHOD( NtAllocateVirtualMemory )
        {
            constexpr SIZE_T requested = 0x3000;
            PVOID ptr = nullptr;
            SIZE_T size = requested;

            auto status = syscall::nt_syscall(
                syscall::get_index( "NtAllocateVirtualMemory" ),
                GetCurrentProcess(),
                &ptr,
                0,
                &size,
                MEM_COMMIT,
                PAGE_EXECUTE_READWRITE
            );

#ifdef USE32
            // Not ready yet
            AssertEx::AreEqual( STATUS_NOT_SUPPORTED, status );
#else
            AssertEx::NtSuccess( status );

            AssertEx::IsNotNull( ptr );
            AssertEx::AreEqual( requested, size );

            VirtualFree( ptr, 0, MEM_RELEASE );
#endif
        }

        TEST_METHOD( NtUserFindWindowEx )
        {
            UNICODE_STRING empty = { };
            UNICODE_STRING wndName = { };

            const auto name = L"BlackBone syscall test " + std::to_wstring( GetCurrentProcessId() );
            // Own the hidden target window so the test does not depend on the user's desktop shell.
            std::unique_ptr<HWND__, decltype(&DestroyWindow)> window(
                CreateWindowExW( 0, L"STATIC", name.c_str(), WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr ),
                &DestroyWindow );
            AssertEx::IsNotNull( window.get() );
            SAFE_CALL( RtlInitUnicodeString, &wndName, name.c_str() );

            auto hwnd = syscall::syscall<HWND>(
                syscall::get_index( IsWindows10OrGreater() ? L"win32u.dll" : L"user32.dll", "NtUserFindWindowEx" ),
                HWND_DESKTOP,
                nullptr,
                &empty,
                &wndName,
                0
                );

#ifdef USE32
            // Not ready yet
            AssertEx::AreEqual( HWND{}, hwnd );
#else
            AssertEx::AreEqual( window.get(), hwnd );

            wchar_t title[MAX_PATH] = { };
            GetWindowTextW( hwnd, title, _countof( title ) );

            AssertEx::AreEqual( name.c_str(), title );
#endif
        }
    };
}
