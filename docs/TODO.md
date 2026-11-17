# TODO

## Upgrade AsmJit to the maintained upstream as a Git submodule

- **Problem**: `src/3rd_party/AsmJit` is a December 2014 snapshot with local patches (`Build.h` trace macros, removed forced exports, a `Config.h` that maps `BLACKBONE_STATIC`/`BLACKBONE_EXPORTS` onto AsmJit linkage). It receives no fixes, and upstream `asmjit/asmjit` has since replaced the whole API (`CodeHolder`, `x86::Assembler`, `Environment`; newer releases also renamed members to snake_case), so the snapshot cannot be swapped in place.
- **Action**: add `asmjit/asmjit` as a submodule pinned to a tagged or reviewed commit, build it from the CMake project with static embedding, and port every code generator: `Asm/AsmHelper32.cpp`, `Asm/AsmHelper64.cpp`, `AsmVariant`/`AsmStack`, `ManualMap/MMap.cpp`, `Process/ProcessModules.cpp`, `Process/RPC/RemoteExec.cpp`, `Process/RPC/RemoteMemory.cpp`, `Process/RPC/RemoteLocalHook.cpp`, local hooks, and the tests. Code emitted for a remote process must be relocated to the remote base address, and a 32-bit build must still emit x64 code for WOW64 targets (`AsmHelper64` from an x86 host).
- **Done when**: `src/3rd_party/AsmJit` is gone, the submodule builds in every CMake preset for x86 and x64, and the native test suite shows no new failures against the pre-upgrade baseline on both architectures, including the WOW64 paths.

## Clear per-target state of `Process` parts on re-attach

- **Problem**: `Process::Detach` resets only `remote()`, `mmap()` and `hooks()`, and those resets do not clear all state bound to the old target. After `Detach` followed by `Attach` to another process:
  - `RemoteExec::reset` leaves `_hijackThread` set by `CreateRPCEnvironment( Worker_UseExisting )`; a later `Worker_CreateNew` environment still routes `ExecInWorkerThread` to that thread, which belongs to the old process, while the code lives in the new one.
  - `NtLdr` has no reset and re-runs `Init` only when the module bitness changes, so a target of the same bitness reuses the old process heap (`_LdrHeapBase`, passed to `RtlAllocateHeap` in the new target) and the old `_nodeMap`.
  - `RemoteLocalHook::Restore` runs only in `~Process`; after a re-attach it writes the saved original bytes and frees the hook block in the new process.
- **Action**: decide the lifetime rule first: destroy created parts in `Detach` (before `_core.Close()`, so `~RemoteLocalHook` restores the hook in the old target; references returned by the accessors then become invalid after `Detach`/`Attach`) or extend each `reset` to clear all target-bound state. Before treating each item as a defect, check `git log -S` for a deliberate upstream rule. Keep `MExcept` VEH memory unowned (`own=false`) so mapped images in the old target keep their handler.
- **Done when**: a re-attach to a second process of the same bitness performs no operation on the first process and uses no cached value from it, verified by a test for the RPC worker mode switch and for manual mapping with loader references after re-attach, on x64 and x86 hosts.
