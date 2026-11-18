#include "../Config.h"
#include "ProcessCore.h"
#include "../Misc/DynImport.h"
#include "../Include/Macro.h"

#ifdef USE32
#include "../Subsystem/Wow64Subsystem.h"
#include "../Subsystem/x86Subsystem.h"
#endif

namespace blackbone
{

#ifdef COMPILER_GCC
#define PROCESS_DEP_ENABLE  0x00000001
#endif

ProcessCore::ProcessCore()
    : _native( nullptr )
{
    // A subsystem over the null handle lets parts run before Attach: the system rejects
    // the handle with an error status instead of the parts dereferencing a null _native
    Init();
}

ProcessCore::~ProcessCore()
{
    Close();
}

/// <summary>
/// Attach to existing process
/// </summary>
/// <param name="pid">Process ID</param>
/// <param name="access">Access mask</param>
/// <returns>Status</returns>
NTSTATUS ProcessCore::Open( DWORD pid, DWORD access )
{
    // Handle current process differently
    _hProcess = (pid == GetCurrentProcessId()) ? GetCurrentProcess() : OpenProcess( access, false, pid );

    // Some routines in win10 do not support pseudo handle
    if (pid == GetCurrentProcessId())
        _hProcess = OpenProcess( PROCESS_ALL_ACCESS, FALSE, pid );

    if (_hProcess)
    {
        _pid = pid;
        return Init();
    }

    return LastNtStatus();
}

/// <summary>
/// Attach to existing process by handle
/// </summary>
/// <param name="handle">Process handle</param>
/// <returns>Status</returns>
NTSTATUS ProcessCore::Open( HANDLE handle )
{
    // pid 0 marks an unattached process, so a handle without one is not taken
    DWORD pid = GetProcessId( handle );
    if (pid == 0)
        return LastNtStatus();

    // Some routines in win10 do not support pseudo handle. The replacement is opened before
    // the caller's handle is taken: on failure the object stays unattached and the caller keeps its handle
    HANDLE owned = handle;
    if (pid == GetCurrentProcessId())
    {
        owned = OpenProcess( PROCESS_ALL_ACCESS, FALSE, pid );
        if (!owned)
            return LastNtStatus();
    }

    // Taking the caller's handle first makes the assignment of the replacement close it
    _hProcess = handle;
    _hProcess = owned;
    _pid = pid;

    return Init();
}


/// <summary>
/// Initialize some internal data
/// </summary>
/// <returns>Status code</returns>
NTSTATUS ProcessCore::Init()
{
#ifdef USE64
    // An x64 build never runs on an x86 OS and is never WOW64, so no runtime choice is needed
    _native = std::make_unique<Native>( _hProcess );
#else
    // Detect x86 OS
    SYSTEM_INFO info = { { 0 } };
    GetNativeSystemInfo( &info );

    if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
    {
        _native = std::make_unique<x86Native>( _hProcess );
    }
    else
    {
        // Detect wow64 barrier
        BOOL wowSrc = FALSE;
        IsWow64Process( GetCurrentProcess(), &wowSrc );

        if (wowSrc == TRUE)
            _native = std::make_unique<NativeWow64>( _hProcess );
        else
            _native = std::make_unique<Native>( _hProcess );
    }
#endif

    // Get DEP info
    // For native x64 processes DEP is always enabled
    if (_native->GetWow64Barrier().targetWow64 == false)
    {
        _dep = true;
    }
    else
    {
        DWORD flags = 0;
        BOOL perm = 0;

        if (SAFE_CALL( GetProcessDEPPolicy, _hProcess, &flags, &perm ))
            _dep = (flags & PROCESS_DEP_ENABLE) != 0;
    }

    return STATUS_SUCCESS;
}

/// <summary>
/// Close current process handle
/// </summary>
void ProcessCore::Close()
{
    _hProcess.reset();
    _native.reset();
    _pid = 0;
}

bool ProcessCore::isProtected()
{
    if (_hProcess)
    {
        _PROCESS_EXTENDED_BASIC_INFORMATION_T<DWORD64> info = { 0 };
        info.Size = sizeof( info );
        
        _native->QueryProcessInfoT( ProcessBasicInformation, &info, sizeof( info ) );
        return info.Flags.IsProtectedProcess;
    }

    return false;
}

}