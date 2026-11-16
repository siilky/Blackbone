# TODO

## Upgrade AsmJit to the maintained upstream as a Git submodule

- **Problem**: `src/3rd_party/AsmJit` is a December 2014 snapshot with local patches (`Build.h` trace macros, removed forced exports, a `Config.h` that maps `BLACKBONE_STATIC`/`BLACKBONE_EXPORTS` onto AsmJit linkage). It receives no fixes, and upstream `asmjit/asmjit` has since replaced the whole API (`CodeHolder`, `x86::Assembler`, `Environment`; newer releases also renamed members to snake_case), so the snapshot cannot be swapped in place.
- **Action**: add `asmjit/asmjit` as a submodule pinned to a tagged or reviewed commit, build it from the CMake project with static embedding, and port every code generator: `Asm/AsmHelper32.cpp`, `Asm/AsmHelper64.cpp`, `AsmVariant`/`AsmStack`, `ManualMap/MMap.cpp`, `Process/ProcessModules.cpp`, `Process/RPC/RemoteExec.cpp`, `Process/RPC/RemoteMemory.cpp`, `Process/RPC/RemoteLocalHook.cpp`, local hooks, and the tests. Code emitted for a remote process must be relocated to the remote base address, and a 32-bit build must still emit x64 code for WOW64 targets (`AsmHelper64` from an x86 host).
- **Done when**: `src/3rd_party/AsmJit` is gone, the submodule builds in every CMake preset for x86 and x64, and the native test suite shows no new failures against the pre-upgrade baseline on both architectures, including the WOW64 paths.
