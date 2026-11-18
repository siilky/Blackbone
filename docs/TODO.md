# TODO

## Upgrade AsmJit to the maintained upstream as a Git submodule

- **Problem**: `src/3rd_party/AsmJit` is a December 2014 snapshot with local patches (`Build.h` trace macros, removed forced exports, a `Config.h` that maps `BLACKBONE_STATIC`/`BLACKBONE_EXPORTS` onto AsmJit linkage). It receives no fixes, and upstream `asmjit/asmjit` has since replaced the whole API (`CodeHolder`, `x86::Assembler`, `Environment`; newer releases also renamed members to snake_case), so the snapshot cannot be swapped in place.
- **Action**: add `asmjit/asmjit` as a submodule pinned to a tagged or reviewed commit, build it from the CMake project with static embedding, and port every code generator: `Asm/AsmHelper32.cpp`, `Asm/AsmHelper64.cpp`, `AsmVariant`/`AsmStack`, `ManualMap/MMap.cpp`, `Process/ProcessModules.cpp`, `Process/RPC/RemoteExec.cpp`, `Process/RPC/RemoteMemory.cpp`, `Process/RPC/RemoteLocalHook.cpp`, local hooks, and the tests. Code emitted for a remote process must be relocated to the remote base address, and a 32-bit build must still emit x64 code for WOW64 targets (`AsmHelper64` from an x86 host).
- **Done when**: `src/3rd_party/AsmJit` is gone, the submodule builds in every CMake preset for x86 and x64, and the native test suite shows no new failures against the pre-upgrade baseline on both architectures, including the WOW64 paths.

## Separate the status from the thread exit code in `RemoteExec::ExecDirect`

- **Problem**: `ExecDirect` returns a `DWORD` that is either an `NTSTATUS` from thread creation or the remote thread's exit code, so a caller cannot tell a failed call from a function that returned the same value. Callers that expect an exit code misread a creation failure:
  - `MMap::CreateActx` on the path that maps an x64 image into a WOW64 target takes a creation status as the activation context handle. On an x64 host it also never detects a `CreateActCtxW` failure, because the 32-bit exit code `0xFFFFFFFF` widened into `uint64_t` never equals `INVALID_HANDLE_VALUE`. The generated code on that path stores no handle (the store is commented out next to a `dw( '\x01\x02' )`), so whether the path works at all is unverified.
  - `ProcessModules::Inject`, in its `LoadLibraryW` fallback, takes a nonzero creation status as a loaded module and then reports the error of the following module lookup instead of the cause. The 32-bit exit code also truncates an x64 `HMODULE`, so a module whose base has zero low 32 bits reads as a failure.
  - `ProcessModules::InjectPureIL` ignores the result; after a creation failure it reads a result slot that nothing wrote and reports success.

  `Process::EnsureInit` is not affected: its thread starts at `NtYieldExecution`, so both values are statuses.
- **Action**: return `call_result_t<DWORD>` (or an `NTSTATUS` with the exit code as an output) and update its callers: `Process::EnsureInit`, `ProcessModules::Inject`, `ProcessModules::InjectPureIL`, `MMap::CreateActx`, and `Samples/Main.cpp`. Before fixing only the status on the `CreateActx` WOW64 path, check whether that path works.
- **Done when**: no caller interprets a thread exit code as a status or the reverse; covered by a test that executes a function returning a failure-like value.

## Report missing RPC environments in `NtLdr` and `MExcept`

- **Problem**: of the eight `RemoteExec::ExecInWorkerThread` calls in `NtLdr` and `MExcept`, five discard the status:
  - `NtLdr::InsertInvertedFunctionTable` rereads the table and reports `STATUS_NOT_FOUND`, losing the cause;
  - `NtLdr::UnloadTLS` and `NtLdr::UnlinkTreeNode` create their own environment, discard the status of both the creation and the call, and report success;
  - `NtLdr::InsertTreeNode` returns `void`;
  - `MExcept::RemoveVEH` frees its allocations and returns success.

  `NtLdr::AddStaticTLSEntry` and `MExcept::CreateVEH` propagate the status, and `NtLdr::AllocateInHeap` falls back to a virtual-memory allocation by design. Public entry points reach the discarding calls without the environment `MMap` creates: `Process::nativeLdr().CreateNTReference()` for a new image allocates the loader entry through the fallback, drops the failed tree insertion, and returns `true`; `ProcessModules::Unlink` reaches `UnlinkTreeNode`. Within `MMap` the environment exists, so there only `RemoveVEH` can lose a real failure.
- **Action**: propagate the status from every discarding call, keeping the `AllocateInHeap` fallback. Following [ADR 0001](adr/0001-process-binds-one-target.md), rule 3, a call without the environment it needs reports `STATUS_INVALID_PARAMETER` rather than creating one implicitly.
- **Done when**: each such call path returns its failure status; covered by a test that calls `CreateNTReference` for a new image without an environment.

## Keep one exception handler per mapped image in `MExcept`

- **Problem**: `MExcept` holds a single `_pVEHCode` and `_hVEH`, and for x64 targets one module table `_pModTable` that the handler code addresses. Each non-partial `CreateVEH` allocates new handler code, registers it, and overwrites both members without removing the previous handler; the previous code block is unowned, so it is neither freed nor tracked. This happens when one `Process` maps several images with separate `MapImage` calls that each need the full handler: x86 images without SafeSEH under DEP, or x64 images whose insertion into the inverted function table failed. Dependencies mapped within one call get `PartialExcept` and register no handler. On unmap, `RemoveVEH` removes only the last handler and frees `_pModTable`, so the earlier handlers stay registered over leaked code and, on x64, over a freed module table.
- **Action**: either share one handler per `MMap` (the x64 handler already reads a module table) or keep the handler per image in `ImageContext`, and remove every registered handler on unmap and on mapping failure. Unmapping currently removes exception support before the remaining images receive `DLL_PROCESS_DETACH`; the chosen ownership must keep a handler alive while its images still run.
- **Done when**: mapping two images that each need the vectored handler and unmapping them leaves no handler registered; covered by a test on x86 and x64 targets.

## Remove manually mapped images from the inverted function table

- **Problem**: `NtLdr::InsertInvertedFunctionTable` adds mapped images that get exception support to the target's `LdrpInvertedFunctionTable(s)` through `RtlInsertInvertedFunctionTable`, for x86 and x64 images; when insertion fails for an x64 image, `MMap` falls back to `RtlAddFunctionTable`. Nothing removes an inverted-table entry: `MMap::DisableExceptions` deletes only the `RtlAddFunctionTable` registration (on x64 it returns `STATUS_NOT_FOUND` when the image went into the inverted table instead) and removes the vectored handler, both on `UnmapAllModules`, which ignores its status, and when static TLS initialization fails after exceptions were enabled. The stale entry describes freed memory, so an exception raised later at an address reused in that range would be dispatched with the unmapped image's exception data, and the entry keeps occupying one of the table's `MaxCount` slots; both consequences follow from the table's role and were not observed.
- **Action**: remove the entry when the image is unmapped or its mapping fails after insertion. `InsertInvertedFunctionTable` can fail after the entry is already inserted (while it builds the synthetic exception directory), so decide removal by whether the table lists the image, not by the insertion status. Use an ntdll removal routine resolved through the ntdll symbol sources like the insertion routine, with the same bitness handling and statuses; the current symbol set has no removal routine. Recent ntdll PDBs name it `RtlxRemoveInvertedFunctionTable` (with `RtlpRemoveInvertedFunctionTableEntry` below it), so confirm the name and calling convention for each supported build.
- **Done when**: after mapping and unmapping an image with exception support, the target's inverted function table no longer lists it, on x86 and x64 targets from both host architectures; covered by a test.

## Support current Windows builds in the driver

- **Problem**: `BBInitDynamicData` (`src/BlackBoneDrv/BlackBoneDrv.c`) fills the offsets of undocumented kernel structures (`EPROCESS` protection, object table, VAD root and mitigation flags; `KTHREAD` previous mode; `ETHREAD` exit status; `KPROCESS` execute options) and the SSDT indices of `NtCreateThreadEx` and `NtTerminateThread` from a table keyed by exact build number: 10586, 14393, 15063, 16299, 17134, 17763, 18362/18363, and 19041. Any other build returns `STATUS_NOT_SUPPORTED`, and `DriverEntry` aborts the load, so the driver does not load on Windows 10 20H2 and later (builds 19042–19045) or on any Windows 11 build. Library features that need the driver (`DriverControl`, driver-backed `RemoteMemory`) are unavailable there.
- **Action**: derive the offsets and indices for the supported builds from the matching `ntoskrnl` symbols, in the same spirit as the ntdll symbol sources, instead of extending the hand-written table; decide which builds are supported and keep `STATUS_NOT_SUPPORTED` for the rest.
- **Done when**: the driver loads and the driver-dependent tests pass on at least one current Windows 10 and one current Windows 11 build in test-signing mode, and the supported range is documented.

## Cover native 32-bit Windows in the ntdll fingerprint catalog

- **Problem**: the catalog's x86 entries come from the WOW64 ntdll of 64-bit Windows (`wntdll.pdb`). The native ntdll of 32-bit Windows 10 is a separate build, Winbindex does not index it, and false unique matches of WOW64 fingerprints against it were never measured, so `SymbolLoader::LoadCatalog` resolves nothing on a 32-bit OS and symbols there come only from PDBs ([ADR 0004](adr/0004-ntdll-fingerprint-catalog.md)).
- **Action**: obtain native x86 ntdll builds of Windows 10 22H2 (for example from cumulative updates in the Microsoft Update Catalog) and their PDBs; measure whether the existing x86 entries resolve them to the PDB RVAs with no foreign unique match; then either enable the catalog on a 32-bit OS or generate separate entries keyed so that WOW64 and native x86 entries cannot apply to each other.
- **Done when**: the catalog resolves the covered native x86 builds to their PDB RVAs in the generator's validation, and `LoadCatalog` on a 32-bit OS applies only entries validated against native x86 builds.
