#include "Common.h"

namespace Testing
{		
    TEST_MODULE_INITIALIZE( ModuleInit )
    {
        InitializeOnce();
    }

    TEST_CLASS( Generic )
    {
    public:
        TEST_METHOD_INITIALIZE( ClassInitialize )
        {
            AssertEx::NtSuccess( _proc.Attach( GetCurrentProcessId() ) );
        }

        TEST_METHOD( PEB )
        {
            _PEB32 peb32 = { };
            _PEB64 peb64 = { };

            auto ppeb32 = _proc.core().peb32( &peb32 );
            auto ppeb64 = _proc.core().peb64( &peb64 );

            AssertEx::IsNotZero( ppeb64 );
            AssertEx::IsNotZero( peb64.Ldr );

            if (_proc.barrier().targetWow64)
            {
                AssertEx::IsNotZero( ppeb32 );
                AssertEx::IsNotZero( peb32.Ldr );
            }
        }

        TEST_METHOD( TEB )
        {
            _TEB32 teb32 = { };
            _TEB64 teb64 = { };

            auto pteb32 = _proc.threads().getMain()->teb( &teb32 );
            auto pteb64 = _proc.threads().getMain()->teb( &teb64 );

            AssertEx::IsNotZero( pteb64 );
            AssertEx::IsNotZero( teb64.ClientId.UniqueThread );

            if (_proc.barrier().targetWow64)
            {
                AssertEx::IsNotZero( pteb32 );
                AssertEx::IsNotZero( teb32.ClientId.UniqueThread );
                AssertEx::IsNotZero( teb32.ClientId.UniqueThread );
            }
        }

        static DWORD CALLBACK VoidFn( void* )
        {
            return 0;
        }

        TEST_METHOD( InvalidHandles )
        {
            Process baseProc;
            baseProc.CreateAndAttach( GetTestHelperHost32() );

            Process proc1;
            AssertEx::NtSuccess( proc1.Attach( baseProc.pid(), PROCESS_ALL_ACCESS & ~PROCESS_CREATE_THREAD ) );
            AssertEx::AreEqual( STATUS_ACCESS_DENIED, proc1.threads().CreateNew( reinterpret_cast<ptr_t>(&VoidFn), 0 ).status );

            Process proc2;
            AssertEx::NtSuccess( proc2.Attach( baseProc.pid(), PROCESS_ALL_ACCESS & ~PROCESS_VM_READ ) );

            PEB_T peb = { };
            AssertEx::IsNotZero( proc2.core().peb( &peb ) );
            AssertEx::IsZero( peb.ImageBaseAddress );
            AssertEx::AreEqual( STATUS_ACCESS_DENIED, LastNtStatus() );

            baseProc.Terminate();
        }

        TEST_METHOD( AttachOnce )
        {
            Process proc;

            // Failed attempts leave the object unattached
            AssertEx::IsFalse( NT_SUCCESS( proc.Attach( 0xFFFFFFFC ) ) );
            AssertEx::AreEqual( STATUS_NOT_FOUND, proc.Attach( L"no-such-process.exe" ) );
            AssertEx::IsFalse( NT_SUCCESS( proc.CreateAndAttach( GetTestHelperDir() + L"\\no-such-file.exe" ) ) );
            AssertEx::AreEqual( STATUS_INVALID_HANDLE, proc.Attach( static_cast<HANDLE>(nullptr) ) );
            AssertEx::AreEqual( 0ul, proc.pid() );
            AssertEx::NtSuccess( proc.Attach( GetCurrentProcessId() ) );

            AssertEx::AreEqual( STATUS_ALREADY_INITIALIZED, proc.Attach( GetCurrentProcessId() ) );
            AssertEx::AreEqual( STATUS_ALREADY_INITIALIZED, proc.Attach( L"no-such-process.exe" ) );

            // A rejected handle stays owned by the caller
            HANDLE handle = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId() );
            AssertEx::AreEqual( STATUS_ALREADY_INITIALIZED, proc.Attach( handle ) );
            AssertEx::AreEqual( GetCurrentProcessId(), GetProcessId( handle ) );
            CloseHandle( handle );

            // The helper path is valid, so only the guard keeps CreateAndAttach from starting it
            auto helpers = Process::EnumByName( L"TestHelper32.exe" ).size();
            AssertEx::AreEqual( STATUS_ALREADY_INITIALIZED, proc.CreateAndAttach( GetTestHelperHost32(), true, false ) );
            AssertEx::AreEqual( helpers, Process::EnumByName( L"TestHelper32.exe" ).size() );

            AssertEx::AreEqual( GetCurrentProcessId(), proc.pid() );
        }

        TEST_METHOD( TerminateStatus )
        {
            Process proc;
            AssertEx::NtSuccess( proc.CreateAndAttach( GetTestHelperHost(), true, false ) );

            // A successful TerminateProcess leaves a stale thread status in place
            SetLastNtStatus( STATUS_ACCESS_DENIED );
            AssertEx::NtSuccess( proc.Terminate() );
        }

        TEST_METHOD( Unattached )
        {
            Process proc;
            uint8_t byte = 0;

            AssertEx::AreEqual( STATUS_INVALID_HANDLE, proc.memory().Read( 0x10000, sizeof( byte ), &byte ) );
            AssertEx::AreEqual( STATUS_INVALID_HANDLE, proc.memory().Read( 0x10000, sizeof( byte ), &byte, true ) );
            AssertEx::AreEqual( STATUS_INVALID_HANDLE, proc.memory().Allocate( 0x1000 ).status );
            AssertEx::AreEqual( STATUS_INVALID_HANDLE, proc.memory().remote().Map( false ) );
            AssertEx::AreEqual( STATUS_INVALID_HANDLE, proc.memory().remote().Map( 0x10000, 0x1000 ) );
            AssertEx::AreEqual( STATUS_INVALID_HANDLE, proc.memory().remote().Unmap() );
            AssertEx::AreEqual( STATUS_INVALID_HANDLE, proc.memory().remote().Unmap( 0x10000, 0x1000 ) );
            AssertEx::AreEqual( STATUS_INVALID_HANDLE, proc.remote().CreateRPCEnvironment( Worker_None, true ) );
            AssertEx::AreEqual( STATUS_INVALID_HANDLE, proc.hooks().Apply( RemoteHook::int3, 0x10000, nullptr ) );
            AssertEx::AreEqual( STATUS_INVALID_HANDLE, proc.EnumHandles().status );
            AssertEx::IsTrue( proc.threads().getAll().empty() );
            AssertEx::IsTrue( proc.modules().GetMainModule() == nullptr );

            // Discovery before Attach would cache zero loader addresses for the later target
            AssertEx::IsFalse( proc.nativeLdr().Init( mt_mod64 ) );
        }

        TEST_METHOD( ExecWithoutEnvironment )
        {
            // Running this code would write its result through the unallocated _userData
            uint8_t code[] = { 0xC3 };
            uint64_t result = 0;

            AssertEx::AreEqual( STATUS_INVALID_PARAMETER, _proc.remote().ExecInNewThread( code, sizeof( code ), result ) );
            AssertEx::AreEqual( STATUS_INVALID_PARAMETER, _proc.remote().ExecInWorkerThread( code, sizeof( code ), result ) );

            auto thread = _proc.threads().getMain();
            AssertEx::AreEqual( STATUS_INVALID_PARAMETER, _proc.remote().ExecInAnyThread( code, sizeof( code ), result, thread ) );
        }

    private:
        Process _proc;
    };
}