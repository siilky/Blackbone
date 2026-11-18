# Ntdll symbols come from a fingerprint catalog first

Status: accepted.

## Context

[ADR 0003](0003-symbols-network-on-request.md) left PDBs as the only source of ntdll internal symbols, so manual mapping without network needs a PDB that some earlier run placed in a local store. The RVAs of these symbols change in about every second or third ntdll update, while the masked bytes of their functions stay the same for a year or more and change only on rare events such as a block reordering or a calling convention change. A table keyed by file hash would go stale with every update; fingerprints of the code would not.

## Decision

1. The library carries a generated catalog of fingerprints: masked bytes of each symbol's function, or of an instruction that addresses a data symbol, for the ntdll builds it was generated from. `SymbolLoader::Load` resolves each field from the catalog first and opens the PDB of a bitness only when the catalog misses a field of that bitness. With full coverage no PDB is opened and no network is used.
2. A fingerprint is accepted only if it matches exactly once in the executable sections of the ntdll file; matches of several entries for one symbol must agree. Anything else is a miss, never a guessed address.
3. Masks cover the bytes under `.reloc` entries, relative `call`/`jmp`/`jcc rel32` operands, and x64 RIP-relative displacements.
4. The catalog holds no calling convention or structure data. A new catalog entry for a symbol marks a new code epoch; its ABI is reviewed by hand before the catalog ships. The x86 `LdrProtectMrdata` adapter in `MExcept` passes its argument both in ECX and on the stack, which covers its change from stdcall to fastcall in 26100.8115 and 28000.1643.
5. `GetSymbolAddress` compares the `TimeDateStamp` and `SizeOfImage` of the target's ntdll with the file the RVAs came from and fails with `STATUS_IMAGE_CHECKSUM_MISMATCH` if they differ, for example after an ntdll update before a reboot, or with the status of a failed header read.
6. The catalog is the source file `src/BlackBone/Symbols/SymbolCatalogData.cpp`, written by `SymbolCatalogGen` from ntdll files and their PDBs and committed. The generator writes nothing unless the final table resolves every symbol of every input build to its PDB RVA and every entry covers at least one input build. It lengthens each new entry until the entry alone resolves no input build to a wrong address, because agreement catches a wrong match only while another entry also matches: the stdcall-epoch x86 `LdrProtectMrdata` prologue also starts `RtlDeregisterSecureMemoryCacheCallback`, which it matched uniquely in 28000.1643 after the function moved to fastcall. `scripts/Get-NtdllBuilds.ps1` downloads the builds and PDBs into a local symbol store for the generator.

## Consequences

- Updating the catalog means downloading the builds, running the generator over all of them, reviewing the ABI of new entries in the diff, and rebuilding; the generator does not merge with the existing table. The network is used only in that preparation. [Regenerate the symbol catalog](../agents/workflow.md#regenerate-the-symbol-catalog) owns the procedure.
- An ntdll build the catalog does not cover falls back to the PDB rules of ADR 0003.
- The wrong-address check covers the input builds only. In a later build that changes a function again, an entry of an older epoch can still be the only unique match on unrelated code and yield a wrong address; regenerating over that build exposes it.
- The x86 entries come from the WOW64 ntdll of 64-bit Windows. The native ntdll of 32-bit Windows is a separate build that was never measured, so the catalog is not applied on a 32-bit OS and symbols there come only from PDBs.
- The `Symbols` tests require the catalog to cover the installed ntdll; a Windows update can fail them until the catalog is regenerated.
- The generator is built only in the static presets, because it uses library internals that the DLL does not export.
