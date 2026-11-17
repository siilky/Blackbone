# Ntdll symbols use the network only on request

Status: accepted.

## Context

Manual mapping calls ntdll internals that have no exports: `LdrpHandleTlsData` for static TLS, `RtlInsertInvertedFunctionTable` and `LdrpInvertedFunctionTable` for exception support, `LdrpReleaseTlsEntry` for unmapping, and `LdrProtectMrdata` for the x86 exception handler. The library loaded their addresses from the PDBs of the installed `ntdll.dll` files and filled the gaps with hand-written byte patterns, one set per Windows version. Without `_NT_SYMBOL_PATH` the PDB search downloaded from the Microsoft symbol server whenever a consumer first needed a symbol, without the application asking for it. The patterns were short, accepted the first match, and were not maintained for current builds.

## Decision

1. PDBs are the only source of ntdll internal symbols; the byte patterns are removed. A generated offline fingerprint database is planned as an additional source.
2. Symbols are RVAs in the ntdll file of the matching bitness. A consumer adds the base of the target's ntdll of that bitness.
3. `LoadSymbols( allowNetwork )` loads the symbols once per process. Only the first load takes effect, whether it comes from `LoadSymbols` or from a consumer that needs a symbol; a consumer loads without network. Later calls return the saved status.
4. Without network permission the library opens the PDB directly from local single-tier symbol stores, `%TEMP%\symbols` and the local stores of `srv*` elements in `_NT_SYMBOL_PATH`, by the GUID and age recorded in the ntdll image. DIA's own search is not used for this, because it also reads `_NT_SYMBOL_PATH` and blocking its symbol server access blocks local `srv*` stores too.
5. With network permission the DIA search runs with the current value of `_NT_SYMBOL_PATH` if set, otherwise with `srv*%TEMP%\symbols*https://msdl.microsoft.com/download/symbols`. DIA also searches the `_NT_SYMBOL_PATH` value from process start.
6. A consumer that needs a missing symbol fails with the loading status if loading failed, and with `STATUS_NOT_FOUND` otherwise. Loading fails with the status of reading the ntdll file, `STATUS_DLL_NOT_FOUND` when DIA cannot be created, `STATUS_NO_SUCH_FILE` when no matching PDB is available, and `STATUS_UNSUCCESSFUL` for other DIA failures. A symbol missing from a loaded PDB does not fail loading.

## Consequences

- An application that relied on the implicit download calls `LoadSymbols( true )` before its first manual mapping. Without a local PDB and without that call, mapping an image with static TLS, or an x86 image with exception support, fails unless `NoTLS` or `NoExceptions` is set; x64 exception support falls back to `RtlAddFunctionTable`, and unmapping an image with static TLS completes but reports that it could not release its loader TLS entry.
- A PDB that an earlier networked load placed in a local store is found later without network.
- "Network" means the symbol server over HTTP(S). File-share stores (`\\server\share`) and mapped network drives in `_NT_SYMBOL_PATH` count as local stores and are read without permission.
- Two-tier stores (`index2.txt`), plain directories, and `cache*` elements are not searched without network, nor is the default store that symsrv uses for a `srv*` element without a local store.
- The tests call `LoadSymbols( true )` once in module initialization.
