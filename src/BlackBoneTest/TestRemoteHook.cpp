#include "Common.h"

namespace Testing
{
    struct HookClass
    {
        ~HookClass()
        {
            // Join the debug thread before callback state is destroyed, including on assertion failure.
            process.hooks().reset();
            process.Terminate();
        }

        void HookNtOpenProcess( RemoteContext& context )
        {
            //
            // Get process ID
            //
            _CLIENT_ID_T<DWORD_PTR> cid = { };
            auto pcid = context.getArg( 3 );
            process.memory().Read( pcid, sizeof( cid ), &cid );
            DWORD pid = static_cast<DWORD>(cid.UniqueProcess);

            if (pid == GetCurrentProcessId())
            {
                // Remove PROCESS_TERMINATE access right
                auto mask = context.getArg( 1 );
                if (mask & PROCESS_TERMINATE)
                {
                    context.setArg( 1, mask & ~PROCESS_TERMINATE );
                    calls++;
                }
            }
        }

        void HookNtAllocateVirtualMemory( RemoteContext& context )
        {
            for (int i = 0; i < 6; i++)
                arguments[i] = context.getArg( i );

            calls++;
        }

        Process process;
        int calls = 0;
        DWORD64 arguments[6] = { };
    };

    TEST_CLASS( RemoteHooking )
    {
    public:
        TEST_METHOD( NtOpenProcess )
        {
            HookClass hooker;

            auto path = GetTestHelperHost();
            AssertEx::IsFalse( path.empty() );


            // Give process some time to initialize
            AssertEx::NtSuccess( hooker.process.CreateAndAttach( path ) );
            Sleep( 100 );

            // Remote helper function
            auto terminatePtr = hooker.process.modules().GetExport( hooker.process.modules().GetMainModule(), "TerminateProc" );
            AssertEx::IsTrue( terminatePtr.success() );

            // Get function
            auto pHookFn = hooker.process.modules().GetNtdllExport( "NtOpenProcess" );
            AssertEx::IsTrue( pHookFn.success() );

            // Hook and try to terminate from remote process
            AssertEx::NtSuccess( hooker.process.hooks().Apply( RemoteHook::hwbp, pHookFn->procAddress, &HookClass::HookNtOpenProcess, hooker ) );

            auto terminate = MakeRemoteFunction<long( *)(DWORD)>( hooker.process, terminatePtr->procAddress );
            auto result = terminate( GetCurrentProcessId() );

            // Remove waits for any callback holding the hook lock before captured state is read.
            hooker.process.hooks().Remove( pHookFn->procAddress );

            AssertEx::IsTrue( result.success() );
            AssertEx::AreEqual( ERROR_ACCESS_DENIED, result.result() );
            AssertEx::AreEqual( 1, hooker.calls );
        }

        TEST_METHOD( NtAllocateVirtualMemory )
        {
            HookClass hooker;

            auto path = GetTestHelperHost();
            AssertEx::IsFalse( path.empty() );

            // Give process some time to initialize
            AssertEx::NtSuccess( hooker.process.CreateAndAttach( path ) );
            Sleep( 100 );

            // Get function
            auto pHookFn = hooker.process.modules().GetNtdllExport( "NtAllocateVirtualMemory" );
            AssertEx::IsTrue( pHookFn.success() );

            PVOID base = nullptr;
            SIZE_T size = 0xDEAD;
            auto NtAllocateVirtualMemory = MakeRemoteFunction<NTSTATUS( __stdcall * )(HANDLE, PVOID*, ULONG_PTR, PSIZE_T, ULONG, ULONG)>( hooker.process, pHookFn->procAddress );

            AssertEx::NtSuccess( hooker.process.remote().CreateRPCEnvironment( Worker_CreateNew, true ) );
            auto worker = hooker.process.remote().getWorker();
            AssertEx::IsTrue( worker != nullptr );

            // Hook and try to call
            AssertEx::NtSuccess( hooker.process.hooks().Apply( RemoteHook::hwbp, pHookFn->procAddress, &HookClass::HookNtAllocateVirtualMemory, hooker, worker ) );
            auto backgroundResult = NtAllocateVirtualMemory.Call( { GetCurrentProcess(), &base, 0, &size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE } );

            base = nullptr;
            size = 0xDEAD;
            auto result = NtAllocateVirtualMemory.Call( { GetCurrentProcess(), &base, 0, &size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE }, worker );

            // Remove waits for any callback holding the hook lock before captured state is read.
            hooker.process.hooks().Remove( pHookFn->procAddress );

            AssertEx::NtSuccess( backgroundResult.status );
            AssertEx::NtSuccess( backgroundResult.result() );
            AssertEx::NtSuccess( result.status );
            AssertEx::NtSuccess( result.result() );
            AssertEx::AreEqual( 1, hooker.calls );
            AssertEx::AreEqual( reinterpret_cast<HANDLE>(hooker.arguments[0]), GetCurrentProcess() );
            AssertEx::AreNotEqual( hooker.arguments[1], DWORD64( 0 ) );
            AssertEx::AreEqual( hooker.arguments[2], DWORD64( 0 ) );
            AssertEx::AreNotEqual( hooker.arguments[3], DWORD64( 0 ) );
            AssertEx::AreEqual( hooker.arguments[4], DWORD64( MEM_RESERVE | MEM_COMMIT ) );
            AssertEx::AreEqual( hooker.arguments[5], DWORD64( PAGE_EXECUTE_READWRITE ) );
        }
    };
}
