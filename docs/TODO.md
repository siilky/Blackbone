# TODO

## Upgrade AsmJit to the maintained upstream as a Git submodule

- **Problem**: `src/3rd_party/AsmJit` is a December 2014 snapshot with local patches (`Build.h` trace macros, removed forced exports, a `Config.h` that maps `BLACKBONE_STATIC`/`BLACKBONE_EXPORTS` onto AsmJit linkage). It receives no fixes, and upstream `asmjit/asmjit` has since replaced the whole API (`CodeHolder`, `x86::Assembler`, `Environment`; newer releases also renamed members to snake_case), so the snapshot cannot be swapped in place.
- **Action**: add `asmjit/asmjit` as a submodule pinned to a tagged or reviewed commit, build it from the CMake project with static embedding, and port every code generator: `Asm/AsmHelper32.cpp`, `Asm/AsmHelper64.cpp`, `AsmVariant`/`AsmStack`, `ManualMap/MMap.cpp`, `Process/ProcessModules.cpp`, `Process/RPC/RemoteExec.cpp`, `Process/RPC/RemoteMemory.cpp`, `Process/RPC/RemoteLocalHook.cpp`, local hooks, and the tests. Code emitted for a remote process must be relocated to the remote base address, and a 32-bit build must still emit x64 code for WOW64 targets (`AsmHelper64` from an x86 host).
- **Done when**: `src/3rd_party/AsmJit` is gone, the submodule builds in every CMake preset for x86 and x64, and the native test suite shows no new failures against the pre-upgrade baseline on both architectures, including the WOW64 paths.

## Recover `RemoteHook` after a failed debugger attach

- **Problem**: `RemoteHook::EnsureDebug` stores the event thread in `_hEventThd` before the thread runs `DebugActiveProcess`. When that call fails, `EventThread` returns without clearing `_hEventThd`, so every later `Apply`/`AddReturnHook` on the same `Process` fails with `STATUS_TOO_MANY_THREADS` instead of retrying or reporting the attach failure, until `reset()` or `EndDebug()`.
- **Action**: make the failed attach observable to `EnsureDebug` (for example, wait for the thread to report the `DebugActiveProcess` result) and leave no stale event thread behind on failure.
- **Done when**: a hook applied to a process that cannot be debugged returns the attach failure status, and a second attempt returns the same status rather than `STATUS_TOO_MANY_THREADS`; covered by a test.

## Separate the status from the thread exit code in `RemoteExec::ExecDirect`

- **Problem**: `ExecDirect` returns a `DWORD` that is either an `NTSTATUS` from thread creation or the remote thread's exit code, so a caller cannot tell a failed call from a function that returned the same value. `Process::EnsureInit` returns it as an `NTSTATUS`.
- **Action**: return `call_result_t<DWORD>` (or an `NTSTATUS` with the exit code as an output) and update its callers: `Process::EnsureInit`, `ProcessModules::Inject`, `ProcessModules::InjectPureIL`, `MMap::CreateActx`, and `Samples/Main.cpp`.
- **Done when**: no caller interprets a thread exit code as a status or the reverse; covered by a test that executes a function returning a failure-like value.

## Keep `ProcessCore::Open( HANDLE )` consistent when reopening the current process

- **Problem**: on Windows 10 and later, `Open( HANDLE )` for the current process replaces the caller's handle with `OpenProcess( PROCESS_ALL_ACCESS )`, closing the caller's handle. If that `OpenProcess` fails, the object keeps the non-zero pid with a null handle, so it counts as attached while every operation fails, and `Attach` cannot be retried.
- **Action**: open the replacement handle before taking the caller's handle, and on failure return the status without changing the object.
- **Done when**: a failed reopen leaves the `Process` unattached and retryable; the behavior is documented at `Attach( HANDLE )`.

## Report missing RPC environments in `NtLdr` and `MExcept`

- **Problem**: several `NtLdr` and `MExcept` paths call `RemoteExec::ExecInWorkerThread` assuming another component (usually `MMap::MapImageInternal`) has created a worker environment. Called through `nativeLdr()` or outside manual mapping, they get `STATUS_INVALID_PARAMETER`, and some callers ignore it (`NtLdr::InsertTreeNode`, `NtLoader.cpp` around the `InsertInvertedFunctionTable` call, and the `MExcept.cpp` call near line 375), so loader structures stay unpatched without an error.
- **Action**: propagate the status from every `ExecInWorkerThread` call in these components, and either create the environment they need or document the precondition on their public entry points.
- **Done when**: each such call path returns or logs its failure status; covered by a test that calls an `NtLdr` operation without a prior manual map.
